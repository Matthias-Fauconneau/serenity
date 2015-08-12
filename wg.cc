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
    uint duration; // seconds FIXME: ->RouteRequest
    uint durationBike; // seconds FIXME: ->RouteRequest
    float score;
    unique<LocationRequest> locationRequest = nullptr;
    function<void(Room&)> loaded;

    void load(const URL& url, Map&& data) {
        const Element root = parseHTML(data);
        const Element& content = root("html")("body")("#main")("#container")("#content");
        if(!content.contains(".result")) return;
        const Element& details = content(".result")(".date-cost").children[3];
        address = copyRef( details(".adress-region").children[2]->content ); // FIXME
        assert_(!address.contains('\n'));
        description = copyRef( details(".mate-content")("p").content );
        //image-content
        profile = copyRef( details(".room-content")("p").content );
        mates = copyRef( details(".person-content")("p").content );
        {TextData s (details(".mate-contact")("p")("a").attribute("onclick"));
            s.skip("Javascript:showContactDetail('");
            string id = s.until('\'');
            getURL(url.relative(URL("/show-contact/search-mate-contact?uuid="+id)), {this, &Room::loadContact}, maximumAge);
        }
        locationRequest.pointer = new LocationRequest(address+",+Zürich", {this, &Room::loadLocation}); // FIXME: does not forward properly through unique<T>(Args...)
     }
    void loadLocation(string, vec3 location) {
        this->location = location;
        loaded(*this);
    }
    void loadContact(const URL&, Map&& data) {
        const Element root = parseHTML(data);
        if(root.children.size<2) return;
        //assert_(root.children.size>=2, root, data.size, url, this->url);
        contact = root.children[0]->content+" <"+root.children[1]->child("a").content+">";
    }
    void loadTransit(const URL&, Map&& data) {
        const Element root = parseXML(data);
        duration = parseInteger(root("DirectionsResponse")("route")("leg")("duration")("value").content);
    }
    void loadBike(const URL&, Map&& data) {
        const Element root = parseXML(data);
        durationBike = parseInteger(root("DirectionsResponse")("route")("leg")("duration")("value").content);
    }
};
template<> inline Room copy(const Room& o) {
    return {copy(o.url), copy(o.postDate), copy(o.startDate), copy(o.untilDate), o.price, copy(o.address), o.location, copy(o.description), copy(o.profile), copy(o.mates),
                copy(o.contact), o.duration, o.durationBike, o.score, nullptr, {}};
}
inline bool operator <(const Room& a, const Room& b) {
    return a.score < b.score;
}

struct WG {
    array<unique<LocationRequest>> requests;
    map<string, vec3> locations;
    void loadLocation(string address, vec3 location) { assert_(location); locations.insert(address, location); }


    WG() {
        for(string address: arguments().slice(1)) {
            unique<LocationRequest> request (nullptr);
            request.pointer = new LocationRequest(address+",+Zürich", {this, &WG::loadLocation}); // FIXME: does not forward properly through unique<T>(Args...)
            requests.append(move(request));
        }
        URL url ("http://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "query=&priceMin=50&priceMax=1500&state=zurich-stadt&permanent=all&student=none&country=ch&orderBy=MetaData%2F%40mgnl%3Alastmodified&orderDir=descending&startSearchMate=true&wgStartSearch=true"__;
        getURL(move(url), {this, &WG::loadIndex}, 1);
    }

    array<Room> index;

    void loadIndex(const URL& url, Map&& data) {
        const Element root = parseHTML(data);
        const auto& list = root("html")("body")("#main")("#container")("#content")("ul");
        for(const Element& li: list.children) {
            const Element& a = li.children[1];
            Room& room = index.append();
            room.loaded = {this, &WG::evaluate};
            room.url = url.relative(a.attribute("href"));
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
            }
        }
        //log(rooms.size, "rooms");
        nextRoom();
    }

    size_t roomIndex = 0;
    Timer timer {{this, &WG::nextRoom}};
    size_t requestCount = 0;

    void nextRoom() {
        struct HTTP;
        extern array<unique<HTTP>> requests;
        while(roomIndex < index.size) {
            Room& room = index[roomIndex];
            //log(room.postDate, room.startDate, room.untilDate, room.price);
            getURL(move(room.url), {&room, &Room::load}, maximumAge);
            roomIndex++;
            if(requests) {
                log(roomIndex+1, "/", index.size);
                requestCount++;
                /*if(requestCount > 512) { timer.setRelative(10*1000); requestCount=0; }
                else*/ timer.setRelative(400);
                return;
            }
        }
        //assert_(rooms.size == index.size, rooms.size, index.size);
        for(size_t i: reverse_range(rooms.size)) {
            Room& room = rooms[i];
            /*uint duration = ::min(room.duration, room.durationBike);
            if(!room.duration) for(string address: arguments().slice(1)) {
                vec3 d = distance(room.location, locations.at(address+",+Zürich"));
                room.duration = d.x;
            }*/
            uint duration = ::min(room.duration, room.durationBike);
            log(str(round(duration/60.))+"min"_/*, str(round(d.y)/1000, 1u)+"km", str(round(d.z))+"m"*/, str(room.price)+"Fr", room.address,
                    room.postDate, room.startDate/*, room.untilDate*/, room.contact,
                    room.url.host.slice(4)+room.url.path/*section(section(room.url.path,'/',-2,-1),'-',0,-3)*/
                    );
        }
    }

    array<Room> rooms; // sorted by score

    void evaluate(Room& room) { // FIXME: might be called before contact is filled
        if(room.price<=410 && find(room.profile,"JUWO"_)) return;
        if(parseDate(room.postDate) <= Date(currentTime()-16*24*60*60)) return;
        //if(parseDate(room.startDate) > Date(currentTime()+37*24*60*60)) return;
        Date until = parseDate(room.untilDate);
        if(until && until < Date(currentTime()+26*24*60*60)) return;

        //bool recent = parseDate(room.postDate) >= Date(currentTime()-24*60*60);
        /*if(!recent) {
            //if(parseDate(room.startDate) > Date(currentTime()+37*24*60*60)) return;
            //Date until = parseDate(room.untilDate);
            //if(until && until < Date(currentTime()+30*24*60*60)) return;
        }*/
        const float c = 17;
        float score = room.price/c;
        for(string address: arguments().slice(1)) {
            vec3 d = distance(room.location, locations.at(address+",+Zürich"));
            score += d.y/17*60/1000;
        }
        //if(!recent && score > 50) return;
        //log(room.address, score, score-room.price/c);
        if(round(score) > 70) return;
        float accScore = room.price/c;
        for(string address: arguments().slice(1)) {
            // Requests route for more accurate evaluation (next time if not already cached)
            Date arrival(currentTime());
            arrival.hours = 19, arrival.minutes = 0, arrival.seconds = 0;
            getURL(URL("https://maps.googleapis.com/maps/api/directions/xml?key="_+key+
                   "&origin="+replace(address," ","+")+",+Zürich"
                   "&destination="+replace(room.address," ","+")+",+Zürich"
                   "&mode=transit&arrival_time="+str((int64)arrival)),
                   {&room, &Room::loadTransit});
            getURL(URL("https://maps.googleapis.com/maps/api/directions/xml?key="_+key+
                   "&origin="+replace(room.address," ","+")+",+Zürich"
                   "&destination="+replace(address," ","+")+",+Zürich"
                   "&mode=bicycling"),
                   {&room, &Room::loadBike});
            uint duration = ::min(room.duration, room.durationBike);
            if(!duration) { accScore=0; break; }
            if(duration > 682) return;
            accScore += duration/60.;
            //log(room.address, duration, accScore, accScore-room.price/c);
        }
        if(accScore > 71) return;
        Room copy = ::copy(room);
        copy.score = accScore?:score;
        rooms.insertSorted(move(copy));
    }
} app;
