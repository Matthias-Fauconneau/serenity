#include "http.h"
#include "xml.h"
struct Hike {
 Hike() {
  Map data = getURL("www.hikr.org/tour/?mode=rss"_);
  const Element root = parseHTML(data);
  log(root("/rss/channel/"));
 }
} app;

