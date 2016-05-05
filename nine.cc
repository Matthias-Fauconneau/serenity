#include "http.h"
#include "xml.h"
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
 unique<Window> window = nullptr;

 Nine() {
  if(!existsFile(".nine")) writeFile(".nine","");
  next();
  window->actions[Space] = {this, &Nine::next};
 }
 void next() {
  if(window) window->presentComplete = {};
  video = Decoder();
  String history = readFile(".nine");
  buffer<string> ids = split(history, "\n");
  URL index ("http://"+arguments()[0]);
  array<string> list;
  for(int unused times: range(11)) {
   Map document = getURL(copy(index));
   Element root = parseHTML(document);
   if(root.XPath("//article", [this, &ids, &list](const Element& e) {
    string id = e["data-entry-id"];
    list.append(id);
    if(ids.contains(id)) return false;
    String caption = e("header").text();
    assert_(caption, e("header"));
    this->caption = caption;
    if(e.XPath("//video", [this](const Element& e) {
              string url = e(0)["src"];
              log(url);
              //getURL(url, {}, 24, HTTP::Content); // Waits for start of content but no need for full file yet
              //video = Decoder(".cache/"+cacheFile(url));
              video = Decoder(url); // Lets libavformat download/block as needed (FIXME: cache)
               if(!window) window = ::window(&layout, int2(0));
              window->presentComplete = [this]{
               Image image = video.read();
               if(image) { this->image = ::move(image); window->render(); }
               else window->presentComplete = {};
              };
              return true;
    }) ||
    e.XPath("//img", [this](const Element& e) {
     log(e["src"]);
     image = decodeImage(getURL(e["src"]));
     return true;
    })) {
     File(".nine", currentWorkingDirectory(), Flags(WriteOnly|Append)).write(id+'\n');
     return true;
    }
    //error(e);
    return false;
   })) {
    if(!window) window = ::window(&layout, int2(0));
    window->render();
    return;
   }
   else {
    index = index.relative(copyRef(root("//.badge-load-more-post")["href"]));
   }
  }
  error("No new items");
 }
} app;
