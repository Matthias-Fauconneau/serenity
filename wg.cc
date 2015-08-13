#include "thread.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "math.h"
#include "time.h"
#include "location.h"

struct Room {
    URL url;
    String postDate;
    String startDate;
    String untilDate;
    uint price;
    String address;
    vec3 location;
    String description;
    String profile;
    String mates;
    String contact;
    buffer<float> durations;
    float score;

    void load() { assert_(url); load(getURL(copy(url), {}, maximumAge)); }
    void load(string data) {
        const Element root = parseHTML(data);
        const Element& content = root("html")("body")("#main")("#container")("#content");
        if(!content.contains(".result")) return;
        const Element& details = content(".result")(".date-cost").children[3];
        address = copyRef( details(".adress-region").children[2]->content ); // FIXME
        if(address.size <= 1) address = copyRef( details(".adress-region").children[3]->content );
        assert_(address.size > 1, details(".adress-region"));
        description = copyRef( details(".mate-content")("p").content );
        //image-content
        profile = copyRef( details(".room-content")("p").content );
        mates = copyRef( details(".person-content")("p").content );
        {TextData s (details(".mate-contact")("p")("a").attribute("onclick"));
            s.skip("Javascript:showContactDetail('");
            string id = s.until('\'');
            getURL(url.relative(URL("/show-contact/search-mate-contact?uuid="+id)), {this, &Room::loadContact}, maximumAge);
        }
        location = LocationRequest(address+",+Zürich").location;
    }
    void loadContact(const URL&, Map&& data) {
        const Element root = parseHTML(data);
        if(root.children.size<2) return;
        //assert_(root.children.size>=2, root, data.size, url, this->url);
        contact = root.children[0]->content+" <"+root.children[1]->child("a").content+">";
    }
};
inline bool operator <(const Room& a, const Room& b) {
    return a.score < b.score;
}

struct WG {
    WG() {
        array<Room> rooms; // Sorted by score

        String destinationFile = readFile("destinations");
        buffer<string> destinations = split(destinationFile,"\n");

        String filter = readFile("filter");
        //buffer<string> filter = apply(split(filterFile,"\n"), [](string s){ return s.contains(' ')?section(s,' '):s; });

        vec3 mainDestination = LocationRequest(destinations[0]+",+Zürich").location;

        URL url ("http://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "query=&priceMin=50&priceMax=1500&state=zurich-stadt&permanent=all&student=none&country=ch&orderBy=MetaData%2F%40mgnl%3Alastmodified&orderDir=descending&startSearchMate=true&wgStartSearch=true"__;
        auto data = getURL(move(url), {}, 1);
        const Element root = parseHTML(data);
        const auto& list = root("html")("body")("#main")("#container")("#content")("ul");
        for(const Element& li: list.children) {
            const Element& a = li.children[1];
            Room room;
            room.url = url.relative(a.attribute("href"));
            assert_(room.url, a);
            room.postDate = copyRef(a.children[0]->children[0]->content);
            {TextData s (a.children[2]->content);
                s.skip("Bis:");
                s.whileAny(" \t\n");
                s.match("bis");
                s.whileAny(" \t\n");
                if(!s.match("Unbefristet")) room.untilDate = copyRef(s.untilEnd());
            }
            room.startDate = copyRef(a.children[2]->children[0]->content);
            {TextData s (a.children[3]->children[0]->content);
                s.skip("SFr. ");
                room.price = s.integer();
                s.skip(".00"_);
                if(room.price <= 210) room.price *= 4;
            }

            // Filters based on data available directly in index to reduce room detail requests
            if(find(filter, section(section(room.url.path,'/',-2,-1),'-',0,-3))) continue;
            //if(parseDate(room.postDate) <= Date(currentTime()-16*24*60*60)) continue;
            Date until = parseDate(room.untilDate);
            if(until && until < Date(currentTime()+34*24*60*60)) continue;
            const float threshold = 1007;
            if(room.price > threshold) continue;

            // Room detail request
            room.load();
            if(find(room.description,"WOKO") && !room.untilDate) {
                assert_(room.price <= 870, room.price, room.url);
                room.price += 100;
            }

            const uint c = 2/*RT*/ * 3600/*Fr/month*//(40*60)/*minutes/week*/; // 3 (Fr week)/(min roundtrip month)

            // Duration to main destination estimate based only on straight distance between locations (without routing)
            float duration = distance(room.location, mainDestination) /1000/*m/km*/ / 17/*km/h*/ * 60 /*min/h*/;
            //log(room.price, 5*duration*c);
            if(room.price + 5*duration*c > 798) continue; // Reduces directions requests

            // Requests route to destinations for accurate evaluation of transit time per day (averaged over a typical week)
            float score = room.price;
            room.durations = buffer<float>(destinations.size);
            for(size_t destinationIndex: range(destinations.size)) {
                TextData s (destinations[destinationIndex]);
                uint roundtripPerWeek = s.integer(); s.whileAny(" \t"); String destination = unsafeRef(s.untilEnd());

                if(destination.contains('|')) { // Type destination
                    /*for(string location: split(destination,"|")) {
                        uint d = DirectionsRequest(room.address,` location, {}, true, true).duration;
                        assert_(d && d < 30*60, d, room.address, location+" near "+room.address);
                        if(!duration) duration = d;
                        duration = ::min(duration, d);
                    }*/
                    auto data = getURL(URL("https://maps.googleapis.com/maps/api/place/nearbysearch/xml?key="_+key+
                                           "&location="+str(room.location.x)+","+str(room.location.y)+"&types="+destination+
                                           "&rankby=distance"), {}, maximumAge);
                    Element root = parseXML(data);
                    //destination = copyRef(root("PlaceSearchResponse")("result")("place_id").content);
                    //string name;
                    for(const Element& result : root("PlaceSearchResponse").children) {
                        if(result.name!="result") continue;
                        //name = result("name").content;
                        //if(ref<string>{"Migrol Service"_,"Knobi"}.contains(result("name").content)) continue;
                        //destination = "place_id:"+result("place_id").content;
                        destination = result("name").content+", "+result("vicinity").content;
                        //log(room.address, name, destination);
                        //const Element& location = result("geometry")("location");
                        /*assert_(!find(destination, "Affoltern am Albis"), root,
                                distance(room.location.xy(), vec2(parseDecimal(location("lat").content), parseDecimal(location("lng").content))),
                                location,
                                LocationRequest(destination).location,
                                distance(room.location.xy(), LocationRequest(destination).location.xy()));*/
                        if(distance(room.location.xy(), LocationRequest(destination).location.xy()) > 22838) continue; // Wrong result in Nearby Search
                        //assert_(room.address != section(destination,','), room.address, result);
                        break;
                    }
                    //assert_(room.address != section(destination,','));
                    uint duration = DirectionsRequest(room.address+", Zürich", destination).duration;
                    DirectionsRequest req(room.address+", Zürich", destination);
                    assert_(/*duration &&*/ duration <= 1352 /*FIXME*/,
                            room.address, //name,
                            destination,//root("PlaceSearchResponse")("result")("name").content,
                            duration, req.bicycling, req.transit);
                } else destination=destination+", Zürich";
                float duration = DirectionsRequest(room.address+", Zürich", destination).duration/60.;
                DirectionsRequest req(room.address+", Zürich", destination);
                assert_(/*duration &&*/ duration < 60, duration, room.address, destination, req.bicycling, req.transit);
                room.durations[destinationIndex] = duration;
                score += roundtripPerWeek*max(3.f,duration)*c;
                //log(score, room.price + 5*duration*c);
                if(score > threshold) break;
            }
            if(score > threshold) continue;
            room.score = score;
            rooms.insertSorted(move(room));
        }
        for(size_t i: reverse_range(rooms.size)) {
            Room& room = rooms[i];
            log(round(room.score), str(apply(room.durations,[](float v){return round(v);}))/*, str(round(d.y)/1000, 1u)+"km", str(round(d.z))+"m"*/, str(room.price)+"Fr", room.address,
                room.postDate, room.startDate/*, room.untilDate*/, room.contact,
                room.url.host.slice(4)+room.url.path/*section(section(room.url.path,'/',-2,-1),'-',0,-3)*/);
        }
    }
} app;
