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
 int64 start = 0;
 String id;

 Nine() {
  if(!existsFile(".nine")) writeFile(".nine","");
  next();
  window->actions[Space] = {this, &Nine::next};
  window->actions[Key(LeftButton)] = {this, &Nine::next};
  window->actions[Return] = [this]{ execute(which("firefox-bin"),{"http://"+arguments()[0]+"/gag/"+id}); };
 }
 void next() {
  if(window) window->presentComplete = {};
  video = Decoder();
  start = 0;
  String history = readFile(".nine");
  buffer<string> ids = split(history, "\n");
  URL index ("http://"+arguments()[0]);
  array<string> list;
  for(int unused times: range(8)) {
   Map document = getURL(copy(index), {}, 1);
   Element root = parseHTML(document);
   if(root.XPath("//article", [this, &ids, &list](const Element& e) {
    string id = e["data-entry-id"];
    list.append(id);
    if(ids.contains(id)) return false;
    this->id = copyRef(id);
    String caption = e("header").text();
    assert_(caption, e("header"));
    this->caption = caption;
    if(e.XPath("//video", [this](const Element& e) {
              string url = e(0)["src"];
              log(url);
              //getURL(url, {}, 24, HTTP::Content); // Waits for start of content but no need for full file yet
              //video = Decoder(".cache/"+cacheFile(url));
              video = Decoder("cache:"+url); // Lets libavformat download/block as needed (FIXME: cache)
              start = 0;
              if(!window) window = ::window(&layout, int2(0));
              window->presentComplete = [this]{
               if(!start) start = realTime();//window->currentFrameCounterValue;
               if(video.videoTime*second > 2*(realTime()/*window->currentFrameCounterValue*/-start)*video.timeDen) {
                window->render(); // Repeat frame (FIXME: get refresh notification without representing same frame)
                return;
               }
               Image image = video.read();
               if(!image) { // Loop
                video.seek(0); video.videoTime = 0;
                while(video.videoTime>(int)video.timeDen/4) video.read(Image());
                image = video.scale();
                log(video.videoTime, video.timeDen);
                start=realTime();
               }
               this->image = ::move(image);
               window->render();
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
