#include "window.h"
#include "algorithm.h"

#if 0
#include "video.h"

uint mean(ref<uint8> Y) { uint sum=0; for(uint8 v: Y) sum+=v; return sum / Y.size; }

struct Background : Widget {
 string baseName = arguments()[0];
 Decoder video = Decoder(baseName+".mkv");
 unique<Window> window = ::window(this);
 int64 clipIndex = 0, frameCount = 0;
 File YUV[3];

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
   if(clipIndex==5) { requestTermination(); return shared<Graphics>(); }
   do { video.read(Image()); } while(mean(video.Y()) < 64); // Skips
   video.scale(window->target);
   if(clipIndex>0) for(size_t i: range(3)) {
    String name = baseName+"."+str(clipIndex)+"."+strx(video.size)+'.'+"YUV"[i];
    if(!existsFile(name)) YUV[i] = File(name, currentWorkingDirectory(), ::Flags(ReadWrite|Create));
   }
   log(clipIndex);
   frameCount = 0; clipIndex++;
  }
  for(size_t i: range(3)) if(YUV[i]) YUV[i].write(cast<byte>(video.YUV(i)));
  //log(frameCount++, file.size());
  return shared<Graphics>();
 }
} app;
#else
#include "guided-filter.h"
#include "png.h"

struct Background : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipIndex = 1, clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 size_t frameIndex = 0, frameCount = 0;

 ImageF mode[3];

 unique<Window> window = nullptr;
 bool toggle = false;

 Time totalTime {true};

 Background() {
  for(string file: currentWorkingDirectory().list(Files)) {
   TextData s(section(file,'.',-3,-2));
   if(startsWith(file, baseName) && s.isInteger() && endsWith(file, ".Y")) {
    int2 size; size.x = s.integer(); s.skip('x'); size.y = s.integer();
    if(!this->size) this->size = size;
    assert_(size == this->size);
    frameCount += File(file).size() / (size.x*size.y);
   }
  }
  assert_(size);

#if 0
  for(size_t i: range(3)) {
   int2 size = this->size / (i?2:1);
   if(!existsFile(baseName+".mode."+"YUV"[i])) { // FIXME: 3D mode
    assert_(frameCount > (1<<8) && frameCount < (1<<16));
    Image8 mode = Image8(size);
    buffer<uint16> histogram(256*mode.ref::size); // 500 MB
    histogram.clear(0);
    for(;;clipFrameIndex++, frameIndex++) {
     Image8 image = this->image(i);
     if(!image) break;
     for(size_t i: range(image.ref::size)) histogram[i*256+image[i]]++;
     log(frameIndex, frameCount);
    }
    clipIndex = 0; frameIndex=0; clipFrameIndex=0;
    for(size_t i: range(mode.ref::size)) mode[i] = argmax(histogram.slice(i*256, 256));
    writeFile(baseName+".mode."+"YUV"[i], cast<byte>(mode));
   }
   Map map(baseName+".mode."+"YUV"[i]);
   Image8 image(unsafeRef(cast<uint8>(map)), size);
   if(i) image = upsample(image);
   mode[i] = gaussianBlur(toFloat(image), R);
  }
#endif

  Image8 images[] {downsample(this->image(0)), this->image(1), this->image(2)};
  Image target(images[0].size);
  totalTime.reset();
  guidedFilter(target, images[0], images[1], images[2]);
  uint64 t = totalTime.reset();
  log(apply(times, [t](const Time& time) { return strD(time, t); }), strD(sum(apply(ref<Time>(times.values), [](const Time& t) { return (uint64)t; })), t), str((double)t/second, 2u)+"s");
  if(1) {
   writeFile("guided.png", encodePNG(target),currentWorkingDirectory(), true);
  } else {
   window = ::window(this, size);
   window->backgroundColor = nan;
   window->presentComplete = [this]{ window->render(); };
   window->actions[Space] = [this] { toggle=!toggle; };
  }
 }

 Image8 image(size_t i) {
  while(clipFrameIndex == clipFrameCount) {
   clipFrameIndex = 0; clipFrameCount = 0;
   String name = baseName+'.'+str(clipIndex)+'.'+strx(size);
   if(!existsFile(name+".Y")) return Image8(); //{ clipIndex++; log(clipIndex); continue; }
   log(name);
   for(size_t i: range(3)) clip[i] = Map(name+'.'+"YUV"[i]);
   assert_(clip[0].size % (size.x*size.y) == 0);
   clipFrameCount = clip[0].size / (size.x*size.y);
   clipIndex++;
  }
  int2 size = this->size / (i?2:1);
  return Image8(unsafeRef(cast<uint8>(clip[i].slice(clipFrameIndex*(size.x*size.y), size.x*size.y))), size);
 }

 vec2 sizeHint(vec2) override { return vec2(size/2); };

 shared<Graphics> graphics(vec2) override {
  Image8 images[] {downsample(this->image(0)), this->image(1), this->image(2)};
  if(images[0]) {
   Image target(images[0].size);
   guidedFilter(target, images[0], images[1], images[2]);
   resize(window->target, target);
   uint64 t = totalTime.reset();
   log(apply(times, [t](const Time& time) { return strD(time, t); }), strD(sum(apply(ref<Time>(times.values), [](const Time& t) { return (uint64)t; })), t), str((double)t/second, 2u)+"s");
   clipFrameIndex++;
   frameIndex++;
  } else {
   requestTermination();
  }
  return shared<Graphics>();
 }
} app;
#endif
