#include "http.h"
#include "xml.h"
#include "png.h"

struct Hike {
 Hike() {
  array<array<vec2>> tracks;
  for(int skip=0; skip<100/*83100*/; skip+=20){
   Map tour = getURL(URL("www.hikr.org/tour/?skip="_+str(skip)));
   const Element list = parseHTML(tour);
   for(const Element& day: list("html/body/#page/#contentmain_swiss/.content-center").children) {
    if(day["class"] != "post-content-final") continue;
    for(const Element& item: day.children) {
     if(item["class"] != "content-list") continue;
     const Element* e = item.child("div/div/strong/a");
     assert_(e, item);
     Map itemContent = getURL((*e)["href"]);
     const Element report = parseHTML(itemContent);
     {const Element* table = report.child("//#geo_table");
      if(!table) continue;
      array<vec2> track;
      for(const Element& row: table->children.slice(1)) {
       const Element& e = row("td/a");
       if(!endsWith(e["href"], ".gpx")) continue;
       Map gpx = getURL(e["href"]);
       const Element gpxTrack = parseXML(gpx);
       {
        const Element* e = gpxTrack.child("gpx/rte");
        if(e) for(const Element& trkpt: e->children) {
         track.append(vec2(parseDecimal(trkpt["lat"]), parseDecimal(trkpt["lon"])));
        }
       }
       {
        const Element* e = gpxTrack.child("gpx/trk");
        if(e) for(const Element& trkseg: e->children) {
         for(const Element& trkpt: trkseg.children) {
          track.append(vec2(parseDecimal(trkpt["lat"]), parseDecimal(trkpt["lon"])));
         }
        }
       }
      }
      tracks.append(::move(track));
     }
    }
   }
  }
  vec2 min = inf, max = -inf;
  for(const ref<vec2> track: tracks) {
   vec2 trackMin = inf, trackMax = -inf;
   for(vec2 p: track) {
    trackMin = ::min(trackMin, p);
    trackMax =::max(trackMax, p);
   }
   if(trackMin.y > 80) continue;
   log((trackMin+trackMax)/2.f);
   min = ::min(min, trackMin);
   max =::max(max, trackMax);
   log(min, max, max-min);
  }
  log(max-min);
  Image map (1366, 768);
  map.clear(0);
  log(tracks.size);
  for(const ref<vec2> track: tracks) {
   log(track.size);
   for(vec2 p: track) {
    vec2 P = vec2(map.size-int2(1))*(p-min)/(max-min);
    P.y = map.size.y-1-P.y; // Positive upward
    int2 i (round(P));
    if(i.x >= 0 && i.x < map.size.x && i.y >= 0 && i.y < map.size.y) map(i.x, i.y) = 0xFF;
   }
  }
  writeFile("map.png",encodePNG(map), currentWorkingDirectory(), true);
 }
} app;

