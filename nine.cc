#include "http.h"
#include "json.h"
#include "window.h"
#include "interface.h"
#include "layout.h"
#include "text.h"
#include "jpeg.h"
#include "video.h"

struct Nine {
 Decoder video;
 Text caption;
 ImageView image;
 HBox layout {{&caption, &image}};
 unique<Window> window = ::window(&layout, int2(0));

 Nine() {
  if(!existsFile(".nine")) writeFile(".nine","");
  window->actions[Space] = {this, &Nine::next};
  next();
 }
 void next() {
  window->presentComplete = {};
  video = Decoder();
  String history = readFile(".nine");
  buffer<string> ids = split(history, "\n");
  Map document = getURL(URL("http://"+arguments()[0]));
  //Element root = parseHTML(document);
  Variant root = parseJSON(document);
  array<string> list;
  for(const Variant& item: root.dict.at("data").list) {
   string id = item.dict.at("id").data;
   list.append(id);
   if(ids.contains(id)) continue;
   //log(item.dict.at("votes").dict.at("count").integer());
   //log(item);
   caption = item.dict.at("caption").data;
   if(item.dict.contains("media") && item.dict.at("media").type != Variant::Boolean) {
    assert_(item.dict.at("media").dict, item);
    String url = unescape(item.dict.at("media").dict.at("mp4").data);
    log(url);
    getURL(url);
    video = Decoder(".cache/"+cacheFile(url));
    window->presentComplete = [this]{
     Image image = video.read();
     if(image) { this->image = ::move(image); window->render(); }
     else window->presentComplete = {};
    };
   }
   else {
    image = decodeImage(getURL(unescape(item.dict.at("images").dict.at("large").data)));
   }
   window->render();
   File(".nine", currentWorkingDirectory(), Flags(WriteOnly|Append)).write(id+'\n');
   return;
  }
  error("No new items", list);
 }
} app;
