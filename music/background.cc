#include "window.h"
#include "interface.h"
#include "image/image.h"

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

/// Converts an sRGB component to linear float
void linear(mref<float> target, ref<uint8> source) { target.apply([](uint8 v) { return sRGB_reverse[v]; }, source); }
inline ImageF linear(ImageF&& target, const Image8& source) { linear(target, source); return move(target); }
inline ImageF linear(const Image8& source) { return linear(source.size, source); }

void sRGB(mref<uint8> target, ref<float> source) { target.apply([](float value) { return sRGB(value); }, source); }
inline Image8 sRGB(const ImageF& value) { Image8 sRGB (value.size); ::sRGB(sRGB, value); return sRGB; }

#if 0
static void bilinear(const Image& target, const Image8& source) {
 const uint stride = source.stride;
 for(size_t y: range(target.height)) {
  for(uint x: range(target.width)) {
   const uint fx = x*256*(source.width-1)/target.width, fy = y*256*(source.height-1)/target.height; //TODO: incremental
   uint ix = fx/256, iy = fy/256;
   uint u = fx%256, v = fy%256;
   const ref<uint8> span = source.slice(iy*stride+ix);
   uint8 d = ( (uint(span[       0]) * (256-u) + uint(span[           1]) * u) * (256-v)
                  + (uint(span[stride]) * (256-u) + uint(span[stride+1]) * u) * (       v) )
                  / (256*256);
   target(x, y) = byte3(d);
  }
 }
}
#endif

Image8 upsample(const Image8& source) {
 size_t w=source.width, h=source.height;
 Image8 target(w*2,h*2);
 for(size_t y: range(h)) for(size_t x: range(w)) {
  target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
 }
 return target;
}

static void bilinear(const Image& target, const ImageF& source) {
 const size_t stride = source.stride;
 for(size_t y: range(target.height)) {
  for(size_t x: range(target.width)) {
   const float fx = x*(source.width-1)/float(target.width), fy = y*(source.height-1)/float(target.height); //TODO: incremental
   size_t ix = fx, iy = fy;
   float u = fract(fx), v = fract(fy);
   const ref<float> span = source.slice(iy*stride+ix);
   float d = ( (span[       0] * (1-u) + span[           1] * u) * (1-v)
                 +(span[stride] * (1-u) + span[stride+1] * u) * (   v) );
   target(x, y) = byte3(sRGB(d));
  }
 }
}

ImageF mean(const ImageF& source, int R) {
 ImageF image = unsafeRef(source);
 for(size_t unused i: range(2)) {
  ImageF target(image.size.yx());
  for(size_t y: range(image.size.y)) {
   float sum = 0;
   for(size_t x: range(R)) sum += image(x, y);
   for(size_t x: range(R)) {
    sum += image(x+R, y);
    target(y, x) = sum / (R+x+1);
   }
   for(size_t x: range(R, image.size.x-R)) {
    sum += image(x+R, y);
    target(y, x) = sum / (2*R+1);
    sum -= image(x-R, y);
   }
   for(size_t x: range(image.size.x-R, image.size.x)) {
    target(y, x) = sum / (R+(image.size.x-1-x)+1);
    sum -= image(x-R, y);
   }
  }
  image = ::move(target);
 }
 return image;
}

ImageF mul(const ImageF& a, const ImageF& b) { return apply(a, b, [](float a, float b){ return a*b;}); }
ImageF sq(const ImageF& x) { return apply(x, [](float x){ return sq(x);}); }
ImageF sqrt(const ImageF& x) { return apply(x, [](float x){ return sqrt(x);}); }

struct Background : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipIndex = 1, clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 size_t frameIndex = 0, frameCount = 0;

 float R = 16;
 float e = 1./256;

 Map modeCache;
 ImageF mode;
 Map medianCache;
 Image8 median;

 unique<Window> window = nullptr;
 bool toggle = false;

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
#elif 1
  if(!existsFile(baseName+".mode")) {
   assert_(frameCount > (1<<8) && frameCount < (1<<16));
   Image8 mode = Image8(size);
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
   this->mode = gaussianBlur(linear(mode), R);
   error(".");
  } else {
   modeCache = Map(baseName+".mode");
   Image8 mode = Image8(unsafeRef(cast<uint8>(modeCache)), size);
   this->mode = gaussianBlur(linear(mode), R);
   //this->mode = linear(mode);
  }
#endif
#if 1
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
   medianCache = Map(baseName+".median");
   median = Image8(unsafeRef(cast<uint8>(medianCache)), size);
  }
#endif
  window = ::window(this, size);
  window->backgroundColor = nan;
  window->presentComplete = [this]{ window->render(); };
  window->actions[Space] = [this] { toggle=!toggle; };
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
 vec2 sizeHint(vec2) override { return vec2(size); };
 shared<Graphics> graphics(vec2) override {
  Image8 YUV[] {this->image(0), upsample(this->image(1)), upsample(this->image(2))};
  if(!YUV[0]) { requestTermination(); return shared<Graphics>(); }
#if 0
  ImageF mask (image.size);
  for(size_t i: range(image.ref::size)) mask[i] = sq(int(image[i])-int(mode[i]));
  for(size_t y: range(mask.size.y)) for(size_t x: range(mask.size.x*7/12, mask.size.x*9/12)) mask(x, y) = sq(255);
  mask = gaussianBlur(mask, R); // TODO: bilateral blur weighted by image
  for(int unused i: range(0)) {
   ImageF target (mask.size);
   for(size_t y: range(1, target.size.y-1)) {
    for(size_t x: range(1, target.size.x-1)) {
     for(int2 d: {int2(1, 0), int2(0,1)}) {
      int v = image(x, y);
      int n = image(x+d.x, y+d.y);
      if(abs(v-n) < 16) {
       target(x, y) = (mask(x,y)+mask(x+d.x,y+d.y)) / 2;
       target(x+d.x, y+d.y) = (mask(x,y)+mask(x+d.x,y+d.y)) / 2;
      }
     }
    }
   }
   mask = ::move(target);
  }
  //const ImageF& blur = mask;
  Image8 target (image.size);
  if(toggle) image = this->image();
  for(size_t y: range(target.size.y)) {
   for(size_t x: range(target.size.x)) {
    int v = image(x, y);
    float m = mask(x,y);
    target(x,y) = m >= 64 ? v : 0;
   }
  }
#else
  ImageF linear = ::linear(source);
  //ImageF blur = unsafeRef(linear);
  ImageF blur = gaussianBlur(linear, this->R);
  //ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b); });
  //ImageF mask = apply(mode, blur, [](float a, float b){ return sq(a-b); });
  ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b) > 1./16; });

  int R = toggle ? 16 : 8;
  const ImageF& I = linear;
  const ImageF& p = mask;
#if 0
  ImageF meanI = ::mean(I, R);
  ImageF meanP = ::mean(p, R);
  ImageF corrII = ::mean(::sq(I), R);
  ImageF corrIP = ::mean(::mul(I, p), R);
  ImageF covII = apply(meanI, corrII, [](float meanI, float corrII){ return corrII - meanI*meanI; });
  ImageF covIP = apply(corrIP, meanI, meanP, [](float corrIP, float meanI, float meanP){ return corrIP - meanI*meanP; });
  ImageF a = apply(covII, covIP, [this](float covII, float covIP){ return covIP/(covII+e); });
  ImageF b = apply(a, meanP, [](float a, float meanP){ return (1-a)*meanP; });
  ImageF meanA = ::mean(a, R);
  ImageF meanB = ::mean(b, R);
  ImageF q = apply(meanA, I, meanB, [](float meanA, float I, float meanB){ return meanA*I + meanB; });
#else
  //const ImageF& q = p;
  const ImageF& q = linear;
#endif
  //ImageF target = apply(q, I, [](float q, float I) { return I*q; });
  ImageF target = unsafeRef(q);
#endif
  bilinear(window->target, target);
  clipFrameIndex++;
  frameIndex++;
  return shared<Graphics>();
 }
} app;
#endif
