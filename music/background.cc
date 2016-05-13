#include "window.h"
#include "algorithm.h"
#include "text.h"

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
 const float blurR = 8;

 unique<Window> window = nullptr;
 bool toggle = false;

 Time totalTime {true};

 Text text;

 ImageF temporal;

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

#if 1
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
   if(!i) image = downsample(image);
   mode[i] = gaussianBlur(toFloat(image), blurR);
  }
  temporal = ImageF(size/2); temporal.clear();
#endif

#if 0
   Image8 images[] {downsample(this->image(0)), this->image(1), this->image(2)};
   Image target(images[0].size);
   totalTime.reset();
   guidedFilter(target, images[0], images[1], images[2]);
   uint64 t = totalTime.reset();
   log(apply(times, [t](const Time& time) { return strD(time, t); }), strD(sum(apply(ref<Time>(times.values), [](const Time& t) { return (uint64)t; })), t), str((double)t/second, 2u)+"s");
   writeFile("guided.png", encodePNG(target),currentWorkingDirectory(), true);
#else
   window = ::window(this, size);
   window->backgroundColor = nan;
   window->presentComplete = [this]{ window->render(); };
   window->actions[Space] = [this] { toggle=!toggle; };
#endif
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

 shared<Graphics> graphics(vec2 wSize) override {
  Image8 images[] {downsample(this->image(0)), this->image(1), this->image(2)};
  if(images[0]) {
   times["toFloat"].start();
   const ImageF I[] {toFloat(images[0]), toFloat(images[1]), toFloat(images[2])};
   times["toFloat"].stop();
   const int2 size = I[0].size;

   const ImageF blur[] {size, size, size};
   for(size_t i: range(3)) gaussianBlur(blur[i], I[i], blurR);
   ImageF mask (size);
   assert_(blur[0].size == mode[0].size && blur[1].size == mode[1].size && blur[2].size == mode[2].size);
   static float mmax = 138;
   float max = mmax;
   for(size_t k: range(mask.ref::size)) {
    float d = sqrt(sq(blur[0][k] - mode[0][k]) + sq(blur[1][k] - mode[1][k]) + sq(blur[2][k] - mode[2][k]));
    max = ::max(max, d);
    mask[k] = d;
   }
   if(max > mmax) { mmax=max; log(mmax); }
   max=mmax;
   //for(size_t k: range(mask.ref::size)) mask[k] *= 1/max;
   for(size_t k: range(mask.ref::size)) mask[k] = mask[k] > max/8 ? 1 : 0;
   for(size_t y: range(mask.size.y)) for(size_t x: range(mask.size.x*7/12, mask.size.x*9/12)) mask(x, y) = 1;

   Image target(images[0].size);
   if(0) {
    ImageF q[3] {size, size, size};
    guidedFilter(ref<ImageF>(q), ref<ImageF>(I), 32, 16);
    times["sRGB"].start();
    sRGBfromBT709(target, q[0], q[1], q[2]);
    times["sRGB"].stop();
   } else {
    mask = guidedFilter(ref<ImageF>(I), mask, 32, 16);
    //for(size_t k: range(mask.ref::size)) mask[k] = mask[k] > 1./2 ? 1 : 0;

    if(!toggle) {
     mask = guidedFilter(ref<ImageF>(I), mask, 8, 256);
     for(size_t k: range(mask.ref::size)) mask[k] = mask[k] > 1./2 ? 1 : 0;

     assert_(temporal.size == mask.size, temporal.size, mask.size);
     const float dt = 1./4;
     for(size_t k: range(mask.ref::size)) temporal[k] = (1-dt)*temporal[k] + dt*mask[k];

     mask = guidedFilter(ref<ImageF>(I), toggle ? mask : temporal, 8, 256);
     for(size_t k: range(mask.ref::size)) mask[k] = mask[k] > 1./2 ? 1 : 0;
    } else {
     mask = guidedFilter(ref<ImageF>(I), mask, 8, 256);
     for(size_t k: range(mask.ref::size)) mask[k] = mask[k] > 1./2 ? 1 : 0;
    }

    for(size_t k: range(mask.ref::size)) {
     I[0][k] *= mask[k];
     I[1][k] = 128+ mask[k]*(I[1][k]-128);
     I[2][k] = 128+ mask[k]*(I[2][k]-128);
    }
    sRGBfromBT709(target, I[0], I[1], I[2]);
   }
   resize(window->target, target);
   if(0) {
    uint64 t = totalTime.reset();
    log(apply(times, [t](const Time& time) { return strD(time, t); }), strD(sum(apply(times.values, [](const Time& t) { return (uint64)t; })), t), str((double)t/second, 2u)+"s");
    for(Time& t: times.values) t.reset();
   }
   clipFrameIndex++;
   frameIndex++;
  } else {
   requestTermination();
  }
  text = str(toggle); text.color = white;
  return text.graphics(wSize);
 }
} app;
#endif
