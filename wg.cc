#include "thread.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "math.h"
#include "time.h"
#include "location.h"
#include "text.h"
#include "interface.h"
#include "layout.h"
#include "window.h"
#include "png.h"

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
    array<String> images;
    buffer<float> durations;
    float score = 0;

    bool evaluate(const float threshold = 1192) {
        // Filters based on data available directly in index to reduce room detail requests

        static String filterFile = readFile("filter");
        bool filter = url && (find(filterFile, section(section(url.path,'/',-2,-1),'.',0,-2)+'\n')
                           || find(filterFile, section(section(url.path,'/',-2,-1),'.',0,-2)+' '));
        //if(filter) return false;
        //if(parseDate(postDate) <= Date(currentTime()-31*24*60*60)) {assert_(!filter); return false;}
        if(startDate && parseDate(startDate) <= Date(currentTime()-31*24*60*60)) {assert_(!filter); return false;}
        Date until = parseDate(untilDate);
        if(until && until < Date(currentTime()+47*24*60*60)) {assert_(!filter); return false;}
        if((score=price) > 865) {assert_(!filter); return false;}

        // Room detail request
        if(url.host) {
            auto data = getURL(copy(url), {}, maximumAge);
            const Element root = parseHTML(data);
            const Element& content = root("html")("body")("#main")("#container")("#content");
            assert_(content.contains(".result"));
            const Element& details = content(".result")(".date-cost").children[3];
            address = copyRef( details(".adress-region").children[2]->content ); // FIXME
            if(address.size <= 1) address = copyRef( details(".adress-region").children[3]->content );
            else {
                String ort = toLower(details(".adress-region").children[3]->content);
                for(string s: ref<string>{"adliswil","binz","dietikon","dietlikon","dübendorf","ehrendingen","fahrweid","glattpark","glattbrugg","kichberg","kloten",
                    "leimbach","meilen","uster","oberengstringen","regensdorf","schlieren","schwerzenbach","wallisellen","wetzikon","8820","8105"})
                    if(find(ort, s)) {assert_(!filter); return false;}
                for(string s: ref<string>{"zürich","zurich","zÜrich","zurigo","oerlikon","zh","affoltern"}) if(find(ort, s)) goto break_;
                error(ort, details(".adress-region").children[3]->content);
                break_:;
            }
            assert_(address.size > 1, details(".adress-region"));
            description = copyRef( details(".mate-content")("p").content );
            if(details.contains(".image-content"))
                for(const Element& a: details(".image-content")(".image").children)
                    images.append(copyRef(a.attribute("href")));
            profile = copyRef( details(".room-content")("p").content );
            mates = copyRef( details(".person-content")("p").content );
            location = LocationRequest(address+",+Zürich").location;
            {TextData s (details(".mate-contact")("p")("a").attribute("onclick"));
                s.skip("Javascript:showContactDetail('");
                string id = s.until('\'');
                auto data = getURL(url.relative(URL("/show-contact/search-mate-contact?uuid="+id)));
                const Element root = parseHTML(data);
                if(root.children.size>=2)
                    contact = root.children[0]->content+" <"+root.children[1]->child("a").content+">";
            }
        }

        /*if((find(description,"WOKO") || find(profile,"WOKO")) && !untilDate) {
            assert_(price <= 870, price, url);
            price += 100 - 70; //+PhD - Utilities included
        }*/
        if((find(profile,"JUWO")||find(profile,"Juwo")) && price<=620) {assert_(!filter); return false;}

        const uint c = 2/*RT*/*3600/*Fr/month*//(40*60)/*minutes/week*/; // 3 (Fr week)/(min roundtrip month)

        static String destinationFile = readFile("destinations");
        static buffer<string> destinations = split(destinationFile,"\n");
        static vec3 mainDestination = LocationRequest(destinations[0]+",+Zürich").location;

        // Duration to main destination estimate based only on straight distance between locations (without routing)
        float duration = distance(location.xy(), mainDestination.xy()) /1000/*m/km*/ / 17/*km/h*/ * 60 /*min/h*/;
        if((score=price + 5*duration*c) > threshold) {assert_(!filter); return false;} // Reduces directions requests

        // Requests route to destinations for accurate evaluation of transit time per day (average over typical week)
        score = price;
        durations = buffer<float>(destinations.size);
        for(size_t destinationIndex: range(destinations.size)) {
            TextData s (destinations[destinationIndex]);
            uint roundtripPerWeek = s.integer(); s.whileAny(" \t"); String destination = unsafeRef(s.untilEnd());

            if(destination.contains('|')) { // Type destination
                auto data = getURL(URL("https://maps.googleapis.com/maps/api/place/nearbysearch/xml?key="_+key+
                                       "&location="+str(location.x)+","+str(location.y)+"&types="+destination+
                                       "&rankby=distance"), {}, maximumAge);
                Element root = parseXML(data);
                for(const Element& result : root("PlaceSearchResponse").children) {
                    if(result.name!="result") continue;
                    destination = result("name").content+", "+result("vicinity").content;
                    vec2 dstLocation = LocationRequest(destination).location.xy();
                    if(distance(location.xy(), dstLocation) > 22838) continue; // Wrong result in Nearby Search
                    break;
                }
                uint duration = DirectionsRequest(address+", Zürich", destination).duration;
                DirectionsRequest req(address+", Zürich", destination);
                assert_(duration <= 1451, duration); // FIXME
            } else destination=destination+", Zürich";
            float duration = DirectionsRequest(address+", Zürich", destination).duration/60.;
            DirectionsRequest req(address+", Zürich", destination);
            assert_(duration < 44, duration);
            durations[destinationIndex] = duration;
            float timeScore = roundtripPerWeek*max(3.f,duration)*c;
            score += timeScore;
            if(score > threshold) {assert_(!filter, durations, score, score-price, price, address, url); return false;}
        }
        assert_(price>=290);
        if(durations[0] > 7 && score-price>356 && price >= 700) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
        if(durations[0] > 8 && score-price>386 && price >= 600) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
        if(durations[0] > 10 && score-price>351 && price >= 500) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
        if(durations[0] > 17 && score-price>422 && price >= 290) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
        if(0) { if(filter) return false; }
        else assert_(filter, durations, score, score-price, price, address, url);
        return true;
    }
};
inline bool operator <(const Room& a, const Room& b) {
    return a.score < b.score;
}

struct WG {
#if 0
    Scroll<Text> text;
    VList<ImageView> images;
    HBox layout {{&text,&images}};
#else
    Text text;
    //UniformGrid<ImageView> images;
    HList<ImageView> images;
    Scroll<VBox> layout {{&text,&images}};
#endif
    unique<Window> window {nullptr};
    array<Room> rooms; // Sorted by score
    size_t roomIndex = -1;

    WG() {
        URL url ("http://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "query=&priceMin=50&priceMax=1500&state=zurich-stadt&permanent=all&student=none"
                   "&country=ch&orderBy=MetaData%2F%40mgnl%3Alastmodified&orderDir=descending"
                   "&startSearchMate=true&wgStartSearch=true"__;
        Map data = getURL(move(url), {}, 1);
        const Element root = parseHTML(data);
        const auto& list = root("html")("body")("#main")("#container")("#content")("ul");
        for(const Element& li: list.children) {
            const Element& a = li.children[1];
            Room room;
            room.url = url.relative(a.attribute("href"));
            assert_(room.url, a, a.attribute("href"));
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

            if(room.evaluate()) rooms.insertSorted(move(room));
        }
        if(1) {
            auto reference = readFile("reference");
            for(string line: split(reference,"\n")) {
                TextData s (line);
                Room room;
                room.price = s.integer();
                s.skip(' ');
                room.address = copyRef(s.untilEnd());
                room.url = room.address;
                assert_(room.evaluate(inf));
                rooms.insertSorted(move(room));
            }
            for(size_t i: reverse_range(rooms.size)) {
                const Room& room = rooms[i];
                log(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(round(room.score-room.price)), str(room.price), room.address,
                    room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_);
            }
            return;
        }
        nextRoom();
        if(!rooms) return;
        window = ::window(&layout, int2(0, 714));
        window->setTitle(str(roomIndex,"/", rooms.size));
        window->actions[Space] = [&](){
            execute(which("xdg-open"),{"http://maps.google.com/maps?q="+rooms[roomIndex].address+", Zürich"});
        };
        window->actions[Return] = [&](){ nextRoom(); };
        window->actions[Delete] = [&](){
            const Room& room = rooms[roomIndex];
            File filter("filter", currentWorkingDirectory(), Flags(WriteOnly|Append));
            filter.write("\n"+str(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(room.score-room.price), str(room.price), room.address,
                                  room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_)
                         +"\n");
            nextRoom();
        };
        //window->setPosition(640/2);
    }
    void nextRoom() {
        if(!rooms) return;
        roomIndex = (roomIndex+1)%rooms.size;
        const Room& room = rooms[roomIndex];
        text = Text(
                    str(round(room.score), round(room.score-room.price), str(apply(room.durations,[](float v){return round(v);})), str(room.price)+"Fr")+"\n"
                    +str("Posted:",room.postDate,"From:", room.startDate)
                    +(room.untilDate?str(" Until:", room.untilDate):""__)+"\n"
                    +str(room.address)+"\n"
                    +room.contact+(room.url ? "\n"_+section(section(room.url.path,'/',-2,-1),'.') : ""_)+"\n"
                    +room.description+"\n"+room.profile+"\n"+room.mates,
                    16, black, 1, 640, "DejaVuSans", true, 1, 0, 0, true);
        images.clear();
        for(size_t imageIndex: range(room.images.size)) {
            if(window) window->setTitle(str(imageIndex+1,"/",room.images.size));
            images.append(decodeImage(getURL(room.url.relative(room.images[imageIndex]))));
        }
        if(window) {
            window->render();
            //window->setTitle(str(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(room.price)+"Fr"));
            window->setTitle(str(roomIndex,"/", rooms.size));
        }
    }
} app;
