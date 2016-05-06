#include "window.h"
#include "interface.h"

#if 0
#include "video.h"

uint mean(ref<uint8> Y) { uint sum=0; for(uint8 v: Y) sum+=v; return sum / Y.size; }

struct Background : Widget {
 Decoder video = Decoder(baseName+".mkv");
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
   //file = File(baseName+"."+str(clipIndex)+"."+strx(video.size), currentWorkingDirectory(), ::Flags(ReadWrite|Create));
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
 string baseName = arguments()[0];
 int2 size;

 size_t clipIndex = 1, clipFrameIndex = 0, clipFrameCount = 0;
 Map clip;

 size_t frameIndex = 0, frameCount = 0;
 //Image32 sum;
 Map cache;
 Image8 mode;
 Image8 median;
 unique<Window> window = nullptr;

 Background() {
  for(string file: currentWorkingDirectory().list(Files)) {
   TextData s(section(file,'.',-2,-1));
   if(startsWith(file, baseName) && s.isInteger()) {
    int2 size; size.x = s.integer(); s.skip('x'); size.y = s.integer();
    if(!this->size) this->size = size;
    assert_(size == this->size);
    frameCount += File(file).size() / (size.x*size.y);
   }
  }
#if 0
  if(!existsFile(baseName+".sum")) {
   sum = Image32(size);
   for(;;clipFrameIndex++, frameIndex++) {
    Image8 image = this->image();
    if(!image) break;
    for(size_t i: range(image.ref::size)) sum[i] += image[i];
    log(frameIndex, frameCount);
   }
   clipIndex = 0;
   writeFile(baseName+".sum", cast<byte>(sum));
  } else {
   cache = Map(baseName+".sum");
   sum = Image32(unsafeRef(cast<uint32>(cache)), size);
  }
#elif 0
  if(!existsFile(baseName+".mode")) {
   assert_(frameCount > (1<<8) && frameCount < (1<<16));
   mode = Image8(size);
   buffer<uint16> histogram(256*mode.ref::size); // 500 MB
   histogram.clear(0);
   for(;;clipFrameIndex++, frameIndex++) {
    Image8 image = this->image();
    if(!image) break;
    for(size_t i: range(image.ref::size)) histogram[i*256+image[i]]++;
    log(frameIndex, frameCount);
   }
   clipIndex = 0;
   for(size_t i: range(mode.ref::size)) mode[i] = argmax(histogram.slice(i*256, 256));
   writeFile(baseName+".mode", cast<byte>(mode));
  } else {
   cache = Map(baseName+".mode");
   mode = Image8(unsafeRef(cast<uint8>(cache)), size);
  }
#else
  if(!existsFile(baseName+".median")) {
   assert_(frameCount > (1<<8) && frameCount < (1<<16));
   median = Image8(size);
   buffer<uint16> histogram(256*median.ref::size); // 500 MB
   histogram.clear(0);
   for(;;clipFrameIndex++, frameIndex++) {
    Image8 image = this->image();
    if(!image) break;
    for(size_t i: range(image.ref::size)) histogram[i*256+image[i]]++;
    log(frameIndex, frameCount);
   }
   clipIndex = 0;
   for(size_t i: range(median.ref::size)) {
    ref<uint16> H = histogram.slice(i*256, 256);
    size_t m = 0, sum = 0; for(;sum<frameCount/2;m++) sum += H[m];
    median[i] = m;
   }
   writeFile(baseName+".median", cast<byte>(median));
  } else {
   cache = Map(baseName+".median");
   median = Image8(unsafeRef(cast<uint8>(cache)), size);
  }
#endif
  window = ::window(this, size);
  window->backgroundColor = nan;
  window->presentComplete = [this]{ window->render(); };
 }
 Image8 image() {
  while(clipFrameIndex == clipFrameCount) {
   clipFrameIndex = 0; clipFrameCount = 0;
   String name = baseName+'.'+str(clipIndex)+'.'+strx(size);
   if(!existsFile(name)) return Image8(); //{ clipIndex++; log(clipIndex); continue; }
   log(name);
   clip = Map(name);
   assert_(clip.size % (size.x*size.y) == 0);
   clipFrameCount = clip.size / (size.x*size.y);
   clipIndex++;
  }
  return Image8(unsafeRef(cast<uint8>(clip.slice(clipFrameIndex*(size.x*size.y), size.x*size.y))), size);
 }
 vec2 sizeHint(vec2) override { return vec2(size); };
 shared<Graphics> graphics(vec2) override {
  Image8 image = this->image();
  if(!image) {  requestTermination(); return shared<Graphics>();  }
  //for(size_t i: range(image.ref::size)) sum[i] += image[i];
  for(size_t y: range(window->target.size.y)) {
   for(size_t x: range(window->target.size.x)) {
    //int v = image(x, y);
    //int m = sum(x, y)/frameCount;
    //int m = mode(x, y);
    int m = median(x, y);
    int f = 0;
    //if(abs(v-m) > 16) f = v; // Foreground
    f = m;
    window->target(x,y) = byte3(f);
   }
  }
  clipFrameIndex++;
  frameIndex++;
  return shared<Graphics>();
 }
} app;
#endif
