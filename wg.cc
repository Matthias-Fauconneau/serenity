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
    float altScore = 0;
    String reason;

    bool evaluate(const float threshold = 728) {
     /*if(0 && url.host && existsFile(cacheFile(url)) && 1) { // Get address for debugging (only when cached and recent enough)
      const Map data = getURL(copy(url));
      const Element root = parseHTML(data);
      const Element& content = root("html")("body")("#main")("#container")("#content");
      assert_(content.contains(".result"), content);
      const Element& details = content(".result")(".date-cost").children[3];
      address = copyRef( details(".adress-region").children[2]->content ); // FIXME
     }*/

        static String negativeFile = readFile("-");
        bool negative;
        assert_(url.path);
        if(url.path.contains('.'))
         negative = (find(negativeFile, section(section(url.path,'/',-2,-1),'.',0,-2)+'\n')
                   || find(negativeFile, section(section(url.path,'/',-2,-1),'.',0,-2)+' ')
                   || endsWith(negativeFile, section(section(url.path,'/',-2,-1),'.',0,-2)));
        else
         negative = find(negativeFile, url.path);
        if(negative /*&& threshold<inf*/) {reason="filter"__+url.path; return false;}

        // Filters based on data available directly in index to reduce room detail requests
        if(postDate && parseDate(postDate) <= Date(currentTime()-31*24*60*60)) {/*assert_(!negative,"date", address);*/ reason=str("post",postDate); return false;}
        if(startDate && parseDate(startDate) <= Date(currentTime()-2*24*60*60)) {/*assert(!negative,"start",startDate,parseDate(startDate));*/ reason="start"__; return false;}
        //if(startDate && parseDate(startDate) > Date(currentTime()+31*24*60*60)) {assert(!negative,"start",startDate,parseDate(startDate)); reason="start"__; return false;}
        Date until;
        if(untilDate && untilDate!="Nach Vereinbarung" && !startsWith(untilDate,"ein Jahr")) {
         TextData s (untilDate);
         until = parseDate(s);
         while(!until) {
          if(!s) break;
          //assert_(s, s);
          s.until(' ');
          until = parseDate(s);
         }
         if(s.match('-') || s.match("bis")) until = parseDate(s);
         if(s.match('(')) s.until(')');
         if(s.match('/')) s.untilEnd();
         while(s) {
          if(s.isInteger()) {
           s.advance(1);
           if(!s.match(' ')) {
            log(s, s.untilEnd());
            until = Date();
            break;
           }
          }
          if(!s.word() && !s.matchAny(" ./")) {
           log(s, s.untilEnd(), until);
           until = Date();
           break;
          }
         }
         assert_(!s, s.data, s.index);
        }
        if(until && until < Date(currentTime()+100*24*60*60)) {assert_(!negative,"until",until); reason=str("until",until); return false;}
        if((score=price) > 550/*500~465*/) {/*assert_(!negative,"price",price);*/ reason="price"__; return false;}

        // Room detail request
        if(url.host) {
         const Map data = getURL(copy(url), {}, 14*24);
            const Element root = parseHTML(data);
            const Element& content = root("html")("body")("#main")("#container")("#content");
            assert_(content.contains(".result"), content);
            const Element& details = content(".result")(".date-cost").children[3];
            address = copyRef( details(".adress-region").children[2]->content ); // FIXME
            address = replace(address, "ö", "oe");
            {size_t i = indexOf(address,", Wohnung "); if(i!=invalid) address = copyRef(address.slice(0, i));}
            if(address.size <= 1) address = copyRef( details(".adress-region").children[3]->content );
            else {
                String ort = toLower(details(".adress-region").children[3]->content);
                for(string s: ref<string>{"8820","8105","adliswil","binz","brüttisellen","dietikon","dietlikon","dübendorf","ehrendingen","fahrweid","gattikon",
                    "glattpark","glattbrugg","gockhausen","kichberg","kloten","küsnacht","leimbach","meilen","oberengstringen","oberglatt","oberrohrdorf",
                    "pfaffhausen","regensdorf","schlieren","schwerzbenbach","schwerzenbach","thalwil","uitikon","uster","wallisellen","wetzikon",
                    "zollikerberg"})
                 if(find(ort, s)) {assert_(s && !negative,"ort", ort, address); reason=unsafeRef(s); return false;}
                for(string s: ref<string>{"zürich","zurich","zÜrich","zurigo","oerlikon","Örlikon","zh","affoltern","wipkingen","seebach"})
                    if(find(ort, s)) goto break_;
                //error(ort, details(".adress-region").children[3]->content, price);
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
        if(negative /*&& threshold<inf*/) {reason="filter "__+url.path; return false;}

        if((find(description,"WOKO") || find(profile,"WOKO")) && !untilDate) {
         assert_(price <= 870, price, url);
         price += 100 - 70; //+PhD - Utilities included
        }

        if((find(profile,"JUWO")||find(profile,"Juwo")) && price<=620) {assert_(!negative); reason="JUWO"__; return false;}

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
        const int maxAccuracy = 2;
        // 0: no routing
        // 1: only route for work
        // 2: route all
        // 3: with schedules
        for(uint accuracy: range(maxAccuracy+1)) {
            const float c = 3600./*Fr/month*//(40*60)/*minutes/week*/; // 1.5 (Fr week)/(min month)
            score = price; altScore = price;
            for(size_t destinationIndex: range(destinations.size)) {
                TextData s (destinations[destinationIndex]);
                if(s.match('#')) { durations[destinationIndex]=0; continue;}
                string dest = s.until(':');
                if(dest.contains('|') && !accuracy) continue;
                assert_(location);
                String destination = dest.contains('|') ? ::nearby(location, dest) : dest+", Zürich";
                String origin = address+", Zürich";
                float durationSum = 0, tripCount = 0;
                // Estimate duration from straight distance between locations (without routing)
                if(accuracy<=1 && destinationIndex >= accuracy) {
                    while(s) { s.whileAny(' '); s.whileNot(' '); tripCount++; }
                    durationSum = tripCount * distance(location, locations[destinationIndex])/1000/*m/km*//(maxAccuracy?45/*35*/:20)/*km/h*/*60/*min/h*/;
                    score += durationSum*c;
                }
                //  route: Requests route to destinations for accurate evaluation of transit time per week
                else {
                    for(size_t tripIndex=0; s && tripIndex<2+2*accuracy; tripIndex++) {
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
                        if(destinationIndex==0 &&
                               (//(duration >= 20 && price>=420) ||
                                //(duration >= 40 && price>=360) ||
                                0)) {assert(!negative, duration, A, B, address, url.path); reason="far"__; return false;}
                        //if(route) log(A, B, duration);
                        assert_(duration < 102, duration, A, B, duration);
                        durationSum += duration; tripCount+=1;
                        /*if(destinationIndex!=0) altScore += duration*c;
                        if(destinationIndex!=1)*/ score += duration*c;
                        if(score > threshold) {assert_(!negative, durations, score, score-price, price, address, url); reason="threshold"__; return false;}
                    }
                    if(s) {
                        float perTrip = durationSum/tripCount;
                        while(s) { s.whileAny(' '); s.whileNot(' '); tripCount++; }
                        float extrapolatedSum = perTrip * tripCount; //-1); // Extrapolates conservatively
                        score += extrapolatedSum - durationSum;
                        durationSum = extrapolatedSum;
                    }
                }
                float perTrip = durationSum/tripCount;
                //if(accuracy) assert_(durations[destinationIndex] <= perTrip, perTrip, durations[destinationIndex], accuracy); // Conservative estimate
                durations[destinationIndex] = perTrip;
                if(score > threshold) {assert_(!negative, durations, score, score-price, price, address, url); reason="threshold"__; return false;}
                //if(altScore > altThreshold) {assert_(!negative, durations, score, score-price, price, address, url); reason="threshold"__; return false;}
                if( //(price >= 699 && round(score-price) >= 193/*248*/ && round(durations[0]) >= 16) ||
                    //(price >= 740 && round(score-price) >= 228 && round(durations[0]) >= 9) ||
                    //(price >= 600 && round(score-price) >= 211 && round(durations[0]) >= 18) ||
                    //(price >= 850 && round(score-price) >= 141 && round(durations[0]) >= 12) ||
                    //(price >= 820 && round(score-price) >= 173 && round(durations[0]) >= 14) ||
                    //(price >= 800 && round(score-price) >= 195 && round(durations[0]) >= 16) ||
                      //(price >= 900 && round(score-price) >= 131 && round(durations[0]) >= 11) ||
                    //round(score-price) > 193 ||
                    0) {
                 assert(!negative, durations, score, score-price, price, address, url);
                 reason = str("S-P", score-price, "D", durations[0]); return false;
                }
            }
        }
        if(1) { if(negative && threshold<inf) {reason="filter"__; return false;} }
        else if(0) assert_(negative, durations, score, score-price, price, address, url);
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
        URL url ("https://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "query=&priceMin=101&priceMax=600&state=zurich-stadt&permanent=all&student=none"
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
                    //if(room.price <= 60) room.price = room.price*40/3;
                    //if(room.price <= 240) room.price = room.price*10/3;
                    if(room.price <= 200) continue;
                    //assert_(room.price > 200, room.price, room.url);
                }

                if(room.evaluate()) rooms.insertSorted(move(room));
                else {
                    bool negative;
                    {static String negativeFile = readFile("-");
                    assert_(url.path);
                    if(url.path.contains('.'))
                     negative = (find(negativeFile, section(section(url.path,'/',-2,-1),'.',0,-2)+'\n')
                               || find(negativeFile, section(section(url.path,'/',-2,-1),'.',0,-2)+' '));
                    else
                     negative = find(negativeFile, url.path);}
                    bool positive;
                    {static File positiveFile ("+", currentWorkingDirectory(), Flags(ReadOnly|Create));
                     static String positiveData = positiveFile.read(positiveFile.size());
                     assert_(url.path);
                     if(url.path.contains('.'))
                         positive = (find(positiveData, section(section(url.path,'/',-2,-1),'.',0,-2)+'\n')
                                     || find(positiveData, section(section(url.path,'/',-2,-1),'.',0,-2)+' '));
                     else
                         positive = find(positiveData, url.path);
                    }
                    assert_(!negative && !positive, room.address, room.reason);
                    if(0 || parseDate(room.postDate) >= currentTime()-2*24*60*60) {
                        assert_(room.reason);
                        log(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(round(room.score-room.price)), str(room.price), room.address,
                            room.postDate, room.startDate, room.untilDate, room.address, room.reason);
                    }
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
        if(existsFile("reference")) {
            auto reference = readFile("reference");
            for(string line: split(reference,"\n")) {
                TextData s (line);
                if(s.match('#')) continue;
                Room room;
                room.price = s.integer();
                s.skip(' ');
                room.address = copyRef(s.untilEnd());
                room.location = ::location(room.address);
                room.url = room.address;
                assert_(room.evaluate(inf), room.address, room.reason, room.score);
                rooms.insertSorted(move(room));
            }
        }
        array<Room> newRooms;
        for(size_t i: reverse_range(rooms.size)) {
         Room& room = rooms[i];
         static String positiveFile = readFile("+");
         bool positive;
         assert_(room.url.path);
         if(room.url.path.contains('.'))
          positive = (find(positiveFile, section(section(room.url.path,'/',-2,-1),'.',0,-2)+'\n')
                    || find(positiveFile, section(section(room.url.path,'/',-2,-1),'.',0,-2)+' '));
         else
          positive = find(positiveFile, room.url.path);
         //if(!positive)
          log(round(room.score), round(room.altScore), str(apply(room.durations,[](float v){return round(v);})),
              str(round(room.score-room.price)), str(round(room.altScore-room.price)), str(room.price), room.address,
             room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_);
         if(!positive || 0) newRooms.append(move(room));
        }
        rooms.clear();
        for(size_t i: reverse_range(newRooms.size)) {
         Room& room = newRooms[i];
         rooms.append(move(room));
        }
        //return;
        nextRoom();
        if(!rooms) return;
        window = ::window(&layout, int2(0, 714));
        window->setTitle(str(roomIndex+1,"/", rooms.size));
        window->actions[Space] = [&](){ execute(which("firefox-bin"),{"http://maps.google.com/maps?q="+rooms[roomIndex].address+", Zuerich"}); };
        window->actions[Return] = [&](){ nextRoom(); };
        window->actions[Key('-')] = [&](){
            const Room& room = rooms[roomIndex];
            File filter("-", currentWorkingDirectory(), Flags(WriteOnly|Append));
            filter.write("\n"+str(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(round(room.score-room.price)), str(room.price), room.address,
                                  room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_)
                         );
            nextRoom();
        };
        window->actions[Key('+')] = [&](){
            const Room& room = rooms[roomIndex];
            File filter("+", currentWorkingDirectory(), Flags(WriteOnly|Append));
            filter.write("\n"+str(round(room.score), round(room.altScore), str(apply(room.durations,[](float v){return round(v);})),
                                  str(round(room.score-room.price)), str(round(room.altScore-room.price)), str(room.price), room.address,
                                  room.postDate, room.startDate, room.untilDate, room.contact, room.url ? section(section(room.url.path,'/',-2,-1),'.') : ""_)
                         +"\n");
            nextRoom();
        };
        window->actions[Tab] = [&](){
            if(images) images.clear();
            else {
                const Room& room = rooms[roomIndex];
                for(size_t imageIndex: range(room.images.size)) {
                    if(window) window->setTitle(str(imageIndex+1,"/",room.images.size));
                    images.append(decodeImage(getURL(room.url.relative(room.images[imageIndex]))));
                }
            }
            if(window) {
                window->render();
                window->setTitle(str(roomIndex+1,"/", rooms.size));
            }
        };
        //window->setPosition(640/2);
    }
    void nextRoom() {
        if(!rooms) return;
        roomIndex = (roomIndex+1)%rooms.size;
        const Room& room = rooms[roomIndex];
        text = Text(
                    str(round(room.score), round(room.altScore), round(room.score-room.price), str(apply(room.durations,[](float v){return round(v);})), str(room.price)+"Fr")+"\n"
                    +str("Posted:",room.postDate,"From:", room.startDate)
                    +(room.untilDate?str(" Until:", room.untilDate):""__)+"\n"
                    +str(room.address)+"\n"
                    +room.contact+(room.url ? "\n"_+section(section(room.url.path,'/',-2,-1),'.') : ""_)+"\n"
                    +room.description+"\n"+room.profile+"\n"+room.mates+"\n"+str(room.images),
                    16, black, 1, 640, "DejaVuSans", true, 1, 0, 0, true);
        images.clear();
        /*for(size_t imageIndex: range(room.images.size)) {
            if(window) window->setTitle(str(imageIndex+1,"/",room.images.size));
            images.append(decodeImage(getURL(room.url.relative(room.images[imageIndex]))));
        }*/
        if(window) {
            window->render();
            //window->setTitle(str(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(room.price)+"Fr"));
            window->setTitle(str(roomIndex+1,"/", rooms.size));
        }
    }
} app;
