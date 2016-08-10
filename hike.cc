#include "http.h"
#include "xml.h"
#include "png.h"
#include "time.h"

float parseDecimal(Data& s) {
 float sign = s.match('-') ? -1 : 1;
 uint64 significand=0, decimal=0;
 for(;;) {
  char c = s.peek();
  if(c>='0' && c<='9') {
   int n = c-'0';
   significand *= 10;
   significand += n;
  } else break;
  s.advance(1);
 }
 if(s.match('.')) for(;;) {
  char c = s.peek();
  if(c>='0' && c<='9') {
   int n = c-'0';
   significand *= 10;
   significand += n;
   decimal++;
  } else break;
  s.advance(1);
 }
 return sign * significand * __builtin_exp2f(float(__builtin_log2(10))*(-int(decimal)));
}

struct Hike {
 Hike() {
  array<array<vec3>> tracks;
  Time ATime, BTime;
  for(int skip=0; skip<2048/*83100*/; skip+=20) {
   log(skip);
   ATime.start();
   Map tour = getURL(URL("www.hikr.org/tour/?skip="_+str(skip)));
   const Element list = parseHTML(tour);
   for(const Element& day: list("html/body/#page/#contentmain_swiss/.content-center").children) {
    if(day["class"] != "post-content-final") continue;
    for(const Element& item: day.children) {
     if(item["class"] != "content-list") continue;
     const Element* e = item.child("div/div/strong/a");
     assert_(e, item);
     string id = section(section((*e)["href"],'/',-2,-1),'.').slice(4);
     Map itemContent = getURL((*e)["href"]);
     const Element report = parseHTML(itemContent);
     {const Element* table = report.child("//#geo_table");
      if(!table) continue;
      static Folder gpxCache(".gpx"_, currentWorkingDirectory(), true);
      if(!existsFile(id, gpxCache) || 0) {
       buffer<vec3> track(16384, 0);
       for(const Element& row: table->children.slice(1)) {
        const Element& e = row("td/a");
        if(!endsWith(e["href"], ".gpx")) continue;
        Map gpx = getURL(e["href"]);
        BTime.start();
        TextData s(gpx);
        for(;;) {
         s.until("<");
         if(!s) break;
         if(!s.match("trkpt") && !s.match("rtept")) continue;
         float lat = 0, lon = 0;
         while(!lat || !lon) {
          if(s.match(" lat=\"")) lat = parseDecimal(s);
          else if(s.match(" lon=\"")) lon = parseDecimal(s);
          else error(""); //s.slice(s.index-32, 64), lat, lon);
          s.skip('"');
         }
         float ele = 0;
         s.match(" ");
         if(!s.match("/>")) {
          //if(!s.match(">")) error(s.slice(s.index-32, 64), lat, lon);
          s.skip(">");
          s.whileAny("\r\n ");
          if(s.match("<ele>")) ele = parseDecimal(s.until('<'));
         }
         assert_(track.size < track.capacity);
         track.append(vec3(lon, lat, ele));
        }
        BTime.stop();
        //assert_(track, (string)gpx);
       }
       writeFile(id, cast<byte>(track), gpxCache, true);
      }
      buffer<vec3> track = cast<vec3>(readFile(id, gpxCache));
      if(track) tracks.append(::move(track));
     }
    }
   }
  }
  log(ATime, BTime);
  /*vec2 min = inf, max = -inf;
  for(const ref<vec3> track: tracks) {
   vec2 trackMin = inf, trackMax = -inf;
   for(vec3 p: track) {
    trackMin = ::min(trackMin, p.xy());
    trackMax =::max(trackMax, p.xy());
   }
   if(trackMin.x > 18 || trackMax.y < 0) continue;
   if(trackMin.y > 57 || trackMax.y < 39) continue;
   log((trackMin+trackMax)/2.f);
   min = ::min(min, trackMin);
   max =::max(max, trackMax);
   log(min, max, max-min);
  }
  log(max-min);*/
  //Image map (1366, 768);
  const vec2 A (8.5417, 47.3769), B (10.7004, 47.5696);
  //const vec2 center (10, 47), size (6.*map.size.x/map.size.y, 6);
  const float R = 6371;
  auto project = [R](vec2 degrees) { return vec2(R*degrees.x*PI/180*sin(degrees.y*PI/180), R*degrees.y*PI/180); };
  const vec2 center = (project(A)+project(B))/2.f;
  const vec2 size = project(B)-project(A);
  Image map (1366, size.y/size.x*1366);
  log(map.size);
  vec2 min = center-size/2.f, max = center+size/2.f;
  map.clear(0);
  log(tracks.size);
  for(const ref<vec3> track: tracks) {
   //log(track.size);
   for(vec3 p: track) {
    vec2 P = vec2(map.size-int2(1))*(project(p.xy())-min)/(max-min);
    P.y = map.size.y-1-P.y; // Positive upward
    int2 i (round(P));
    if(i.x >= 0 && i.x < map.size.x && i.y >= 0 && i.y < map.size.y) map(i.x, i.y) = 0xFF;
   }
  }
  writeFile("map.png",encodePNG(map), currentWorkingDirectory(), true);
 }
} app;

