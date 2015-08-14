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

 bool evaluate(const float threshold = 1007) {
  static String filter = readFile("filter");
  // Filters based on data available directly in index to reduce room detail requests
  if(url && find(filter, section(section(url.path,'/',-2,-1),'-',0,-3))) return false;
  //if(parseDate(room.postDate) <= Date(currentTime()-16*24*60*60)) continue;
  Date until = parseDate(untilDate);
  if(until && until < Date(currentTime()+34*24*60*60)) return false;
  if((score=price) > threshold) return false;

  // Room detail request
  if(url) {
   auto data = getURL(copy(url), {}, maximumAge);
   const Element root = parseHTML(data);
   const Element& content = root("html")("body")("#main")("#container")("#content");
   assert_(content.contains(".result"));
   //if(!content.contains(".result")) return false;
   const Element& details = content(".result")(".date-cost").children[3];
   address = copyRef( details(".adress-region").children[2]->content ); // FIXME
   if(address.size <= 1) address = copyRef( details(".adress-region").children[3]->content );
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

  const uint c = 2/*RT*/*3600/*Fr/month*//(40*60)/*minutes/week*/; // 3 (Fr week)/(min roundtrip month)

  static String destinationFile = readFile("destinations");
  static buffer<string> destinations = split(destinationFile,"\n");
  static vec3 mainDestination = LocationRequest(destinations[0]+",+Zürich").location;

  // Duration to main destination estimate based only on straight distance between locations (without routing)
  float duration = distance(location.xy(), mainDestination.xy()) /1000/*m/km*/ / 17/*km/h*/ * 60 /*min/h*/;
  if((score=price + 5*duration*c) > 1124/*1007+117*/) return false; // Reduces directions requests

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
   score += roundtripPerWeek*max(3.f,duration)*c;
   if(score > 1124/*1007+117*/) return false;
  }
  if(score > 1124/*1007+117*/) return false;
  return true;
 }
 String info() const {
  return str(round(score), str(apply(durations,[](float v){return round(v);})), str(price)+"Fr", address,
     postDate, startDate, untilDate, contact, url ? "\n"_+url.host.slice(4)+url.path : ""_);
 }
};
inline bool operator <(const Room& a, const Room& b) {
 return a.score < b.score;
}

struct WG {
 Text text;
 HList<ImageView> images;
 VBox layout {{&text,&images}};
 unique<Window> window {nullptr};
 array<Room> rooms; // Sorted by score

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
  /*auto reference = readFile("reference");
  for(string line: split(reference,"\n")) {
   TextData s (line);
   Room room;
   room.price = s.integer();
   s.skip(' ');
   room.address = copyRef(s.untilEnd());
   room.evaluate(inf);
   rooms.insertSorted(move(room));
  }*/
  for(size_t i: reverse_range(rooms.size)) {
   const Room& room = rooms[i];
   log(room.info());
   /*log(room.description);
   log(room.profile);
   log(room.mates);
   if(room.images) log(room.images);
   log("maps.google.com/maps?q="+room.address);*/
  }
  if(!rooms) return;
  const Room& room = rooms[0];
  text = Text(
     str(round(room.score), str(apply(room.durations,[](float v){return round(v);})), str(room.price)+"Fr")+"\n"
     +str("Posted:",room.postDate, "\tFrom:", room.startDate)
     +(room.untilDate?str("\tUntil:", room.untilDate):""__)+"\n"
     +str(room.address)+"\n"
     +room.contact+(room.url ? "\n"_+/*room.url.host.slice(4)+*/section(section(room.url.path,'/',-2,-1),'.') : ""_)+"\n"
     +room.description+"\n"+room.profile+"\n"+room.mates+"\n",
     16, black, 1, 1050/2, "DejaVuSans", true, 1, 0, 1050/2, true);
  images.clear();
  for(string image: room.images) {
   images.append(decodeImage(getURL(room.url.relative(image))));
  }
  window = ::window(&layout, int2(1050, -1));
  window->actions[Space] = [&](){
   execute(which("xdg-open"),{"http://maps.google.com/maps?q="+room.address+", Zürich"});
  };
  window->setPosition(1050/4);
 }
} app;
