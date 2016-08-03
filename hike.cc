#include "http.h"
#include "xml.h"

struct Hike {
 Hike() {
  Map rss = getURL("www.hikr.org/tour/?mode=rss"_);
  const Element list = parseHTML(rss);
  for(const Element& item: list("rss/channel").children) {
   if(item.name != "item") continue;
   Map html = getURL(item.content);
   const Element report = parseHTML(html);
   Map gpx = getURL(report("//#geo_table/tr/td/a")["href"]);
   const Element track = parseXML(gpx);
   for(const Element& trkpt: track("gpx/trk/trkseg").children) {
    log(parseDecimal(trkpt["lat"]), "\t", parseDecimal(trkpt["lon"]));
   }
   break;
  }
 }
} app;

