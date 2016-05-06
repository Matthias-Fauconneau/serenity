#include "window.h"
#include "interface.h"

#if 0
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
   //assert_(clipIndex < 11);
   //file = File(arguments()[0]+"."+str(clipIndex)+"."+strx(video.size), currentWorkingDirectory(), ::Flags(ReadWrite|Create));
   //log(clipIndex);
   //frameCount = 0; clipIndex++;
  }
  //if(file) file.write(cast<byte>(video.Y()));
  //log(frameCount++, file.size());
  return shared<Graphics>();
 }
} app;
#else
struct Background : Widget {
 int2 size;

 size_t clipIndex = 1, frameIndex = 0, clipFrameCount = 0;
 Map clip;

 size_t frameCount = 0;
 Image32 sum;
 unique<Window> window = nullptr;

 Background() {
  for(string file: currentWorkingDirectory().list(Files)) {
   if(startsWith(file, arguments()[0]) && !endsWith(file, "mkv")) {
    TextData s(section(file,'.',-2,-1));
    size.x = s.integer(); s.skip('x'); size.y = s.integer();
   }
  }
  sum = Image32(size);
  window = ::window(this, size);
  window->backgroundColor = nan;
  window->presentComplete = [this]{ window->render(); };
 }
 vec2 sizeHint(vec2) override { return vec2(size); };
 shared<Graphics> graphics(vec2) override {
  while(frameIndex == clipFrameCount) {
   frameIndex = 0; clipFrameCount = 0;
   String name = arguments()[0]+'.'+str(clipIndex)+'.'+strx(size);
   if(!existsFile(name)) { requestTermination(); return shared<Graphics>(); } //{ clipIndex++; log(clipIndex); continue; }
   log(name);
   clip = Map(name);
   assert_(clip.size % (size.x*size.y) == 0);
   clipFrameCount = clip.size / (size.x*size.y);
   clipIndex++;
  }
  frameCount++;
  Image8 Y (unsafeRef(cast<uint8>(clip.slice(frameIndex*(size.x*size.y), size.x*size.y))), size);
  for(size_t i: range(Y.ref::size)) sum[i] += Y[i];
  for(size_t y: range(window->target.size.y)) {
   for(size_t x: range(window->target.size.x)) {
    //window->target(x,y) = byte3(Y(x, y));
    window->target(x,y) = byte3(sum(x, y)/frameCount);
   }
  }
  frameIndex++;
  return shared<Graphics>();
 }
} app;
#endif
