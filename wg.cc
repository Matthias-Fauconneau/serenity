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
    uint price = 0;
    String address;
    vec2 location;
    String description;
    String profile;
    String mates;
    String contact;
    array<String> images;
    buffer<float> durations;
    float score = 0;
    String reason;

    bool evaluate(const float threshold = 1000/*367-1192*/) {
        static String filterFile = readFile("WG");
        bool filter = url && (find(filterFile, section(section(url.path,'/',-2,-1),'.',0,-2)+'\n')
                           || find(filterFile, section(section(url.path,'/',-2,-1),'.',0,-2)+' '));
        //if(filter) {reason="filter"__; return false;}

        // Filters based on data available directly in index to reduce room detail requests
        //if(parseDate(postDate) <= Date(currentTime()-31*24*60*60)) {assert_(!filter); return false;}
        if(startDate && parseDate(startDate) < Date(currentTime()-31*24*60*60)) {assert_(!filter,"start",startDate,parseDate(startDate)); reason="start"__; return false;}
        Date until = parseDate(untilDate);
        if(until && until < Date(currentTime()+62*24*60*60)) {assert_(!filter,"until",until); reason=str("until",until); return false;}
        if((score=price) > 600/*865,1007*/) {assert_(!filter,"price",price); reason="price"__; return false;}

        // Room detail request
        if(url.host) {
            const Map data = getURL(copy(url), {}, 7*24);
            const Element root = parseHTML(data);
            const Element& content = root("html")("body")("#main")("#container")("#content");
            assert_(content.contains(".result"), content);
            const Element& details = content(".result")(".date-cost").children[3];
            address = copyRef( details(".adress-region").children[2]->content ); // FIXME
            if(address.size <= 1) address = copyRef( details(".adress-region").children[3]->content );
            else {
                String ort = toLower(details(".adress-region").children[3]->content);
                for(string s: ref<string>{"8820","8105","adliswil","binz","dietikon","dietlikon","dübendorf","ehrendingen","fahrweid","gattikon",
                    "glattpark","glattbrugg","gockhausen","kichberg","kloten","küsnacht","leimbach","meilen","oberengstringen","oberglatt","oberrohrdorf",
                    "pfaffhausen","regensdorf","schlieren","schwerzbenbach","schwerzenbach","thalwil","uitikon","uster","wallisellen","wetzikon",
                    "zollikerberg"})
                    if(find(ort, s)) {assert_(!filter); reason=unsafeRef(s); return false;}
                for(string s: ref<string>{"zürich","zurich","zÜrich","zurigo","oerlikon","Örlikon","zh","affoltern","wipkingen","seebach"})
                    if(find(ort, s)) goto break_;
                error(ort, details(".adress-region").children[3]->content, price);
                break_:;
            }
            assert_(address.size > 1, details(".adress-region"));
            description = copyRef( details(".mate-content")("p").content );
            if(details.contains(".image-content"))
                for(const Element& a: details(".image-content")(".image").children)
                    images.append(copyRef(a.attribute("href")));
            profile = copyRef( details(".room-content")("p").content );
            mates = copyRef( details(".person-content")("p").content );
            location = ::location(address+",+Zürich");
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
        if((find(profile,"JUWO")||find(profile,"Juwo")) && price<=620) {assert_(!filter); reason="JUWO"__; return false;}
        if(startsWith(profile,"eine nette gesellige Mitbewohnerin"_)) {assert_(!filter); reason="MitbewohnerIN"__; return false;}

        static String destinationFile = readFile("destinations");
        static buffer<string> destinations = split(destinationFile,"\n");
        static buffer<vec2> locations;
        if(!locations) {
            locations = buffer<vec2>(destinations.size);
            for(size_t destinationIndex: range(destinations.size)) {
                TextData s (destinations[destinationIndex]);
                string destination = s.until(':');
                locations[destinationIndex] = destination.contains('|') ? 0 : ::location(destination+", Zürich");
            }
        }
        durations = buffer<float>(destinations.size);
        const int maxAccuracy = 0;
        for(uint accuracy: range(maxAccuracy+1)) {
            const float c = 3600./*Fr/month*//(40*60)/*minutes/week*/; // 1.5 (Fr week)/(min month)

            score = price;
            for(size_t destinationIndex: range(destinations.size)) {
                TextData s (destinations[destinationIndex]);
                string dest = s.until(':');
                if(dest.contains('|') && !accuracy) continue;
                String destination = dest.contains('|') ? ::nearby(location, dest) : dest+", Zürich";
                String origin = address+", Zürich";
                float durationSum = 0, tripCount = 0;
                // Estimate duration from straight distance between locations (without routing)
                if(accuracy == 0) {
                    while(s) { s.whileAny(' '); s.whileNot(' '); tripCount++; }
                    durationSum = tripCount * distance(location, locations[destinationIndex])/1000/*m/km*//(maxAccuracy?35:20)/*km/h*/*60/*min/h*/;
                    score += durationSum*c;
                }
                //  route: Requests route to destinations for accurate evaluation of transit time per week
                else {
                    for(size_t tripIndex=0; s && tripIndex<2+4*accuracy; tripIndex++) {
                        s.whileAny(' ');
                        bool outbound = false;
                        if(s.match('-')) outbound = true;
                        static Date day = parseDate("13/08");
                        Date date = parseDate(s.whileNo(" -"_));
                        date.year = day.year, date.month = day.month, date.day = day.day;
                        int64 time = date;
                        string A = origin, B = destination;
                        if(outbound) time=-time;
                        else { s.skip('-'); swap(A, B); }
                        float duration = ::duration(A, B, time)/60.;
                        //if(route) log(A, B, duration);
                        assert_(duration < 52, duration);
                        durationSum += duration; tripCount+=1;
                        score += duration*c;
                        if(score > threshold) {assert_(!filter, durations, score, score-price, price, address, url); reason="threshold"__; return false;}
                    }
                    if(s) {
                        float perTrip = durationSum/tripCount;
                        while(s) { s.whileAny(' '); s.whileNot(' '); tripCount++; }
                        float extrapolatedSum = perTrip * (tripCount-1); // Extrapolates conservatively
                        score += extrapolatedSum - durationSum;
                        durationSum = extrapolatedSum;
                    }
                }
                float perTrip = durationSum/tripCount;
                if(accuracy) assert_(durations[destinationIndex] <= perTrip, perTrip, durations[destinationIndex], accuracy); // Conservative estimate
                durations[destinationIndex] = perTrip;
                if(score > threshold) {assert_(!filter, durations, score, score-price, price, address, url); reason="threshold"__; return false;}
            }
        }
        assert_(price>=210, durations[0], score-price, price);
        if(parseDate(postDate) <= Date(currentTime()-24*60*60)) {
            //return false; //FIXME
            //if(durations[0] > 7 && score-price>356 && price >= 700) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
            //if(durations[0] > 8 && score-price>386 && price >= 600) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
            //if(durations[0] > 10 && score-price>351 && price >= 500) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
            //if(durations[0] > 17 && score-price>422 && price >= 210) { assert_(!filter, durations, score, score-price, price, address, url); return false; }
        }
        if(0) { if(filter) {reason="filter"__; return false;} }
        else if(0) assert_(filter, durations, score, score-price, price, address, url);
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

        if(arguments().size == 1) {
            const Map data = getURL(copy(url), {}, 1);
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
                else if(parseDate(room.postDate) >= currentTime()-2*24*60*60) {
                    assert_(room.reason);
                    log(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(round(room.score-room.price)), str(room.price), room.address,
                        room.postDate, room.startDate, room.untilDate, room.reason);
                }
            }
        } else {
            Room room;
            string id = arguments()[1];
            room.url = url.relative(URL("/search/mate/ch/zurich-stadt/"+id));
            room.startDate = copyRef(section(id,'-',0,3));
            room.untilDate = copyRef(section(id,'-',3,4));
            room.price = parseInteger(copyRef(section(id,'-',4,5)));
            room.evaluate();
            rooms.append(move(room));
        }
        if(0) {
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
        }
        for(size_t i: reverse_range(rooms.size)) {
            const Room& room = rooms[i];
            log(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(round(room.score-room.price)), str(room.price), room.address,
                room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_);
        }
        //return;
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
            File filter("WG", currentWorkingDirectory(), Flags(WriteOnly|Append));
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
