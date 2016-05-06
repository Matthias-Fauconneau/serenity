#include "window.h"
#include "interface.h"
#include "video.h"

uint mean(ref<uint8> Y) { uint sum=0; for(uint8 v: Y) sum+=v; return sum / Y.size; }

struct Background : Widget {
 Decoder video = Decoder(arguments()[0]+".mkv");
 unique<Window> window = ::window(this);
 //int64 clipIndex = 0, frameCount = 0;
 //File file;

 Background() {
  //video.seek(20000);
  //while(video.videoTime < 20000) { video.read(Image()); log(video.videoTime); } // FIXME
  window->backgroundColor = nan;
  window->presentComplete = [this]{ window->render(); };
 }
 vec2 sizeHint(vec2) override { return vec2(video.size/4); };
 shared<Graphics> graphics(vec2) override {
  //if(!start) start = realTime(); //window->currentFrameCounterValue;
  /*if(video.videoTime*second > (realTime()-start)*video.timeDen) {
    window->render(); // Repeat frame (FIXME: get refresh notification without representing same frame)
    return;
   }*/
  video.read(window->target); // HACK
  if(mean(video.Y()) < 32) {
   do { video.read(Image()); } while(mean(video.Y()) < 64); // Skips
   video.scale(window->target);
   //assert_(clipIndex < 8);
   //file = File(arguments()[0]+"."+str(clipIndex)+"."+strx(video.size), currentWorkingDirectory(), ::Flags(ReadWrite|Create));
   //log(clipIndex);
   //frameCount = 0; clipIndex++;
  }
  //if(file) file.write(cast<byte>(video.Y()));
  //log(frameCount++, file.size());
  return shared<Graphics>();
 }
} app;
