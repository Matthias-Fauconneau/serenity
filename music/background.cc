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

void toFloat(mref<float> target, ref<uint8> source) { target.apply([](uint8 v) { return v; }, source); }
inline ImageF toFloat(ImageF&& target, const Image8& source) { toFloat(target, source); return move(target); }
inline ImageF toFloat(const Image8& source) { return toFloat(source.size, source); }

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

/*static void bilinear(const Image& target, const ImageF& source) {
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
}*/

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

const double Kb = 0.0722, Kr = 0.2126;
const double rv = (1-Kr)*255/112;
const double gu = (1-Kb)*Kb/(1-Kr-Kb)*255/112;
const double gv = (1-Kr)*Kr/(1-Kr-Kb)*255/112;
const double bu = (1-Kb)*255/112;

Image sRGB709(const ImageF& Y, const ImageF& U, const ImageF& V) {
 Image target(Y.size);
 for(size_t i: range(Y.ref::size)) {
  int y = (int(Y[i]) - 16)*255/219;
  int Cb = int(U[i]) - 128;
  int Cr = int(V[i]) - 128;
  int r = y                        + Cr*rv;
  int g = y - Cb*gu - Cr*gv;
  int b = y + Cb*bu;
  target[i] = byte4(clamp(0,b,255), clamp(0,g,255), clamp(0,r,255));
 }
 return target;
}

Image sRGB709(const Image8& Y, const Image8& U, const Image8& V) {
 const int rv = ::rv*65536;
 const int gu = ::gu*65536;
 const int gv = ::gv*65536;
 const int bu = ::bu*65536;
 Image target(Y.size);
 for(size_t i: range(Y.ref::size)) {
  int y = (int(Y[i]) - 16)*255/219;
  int Cb = int(U[i]) - 128;
  int Cr = int(V[i]) - 128;
  int r = y                        + Cr*rv/65536;
  int g = y - Cb*gu/65536 - Cr*gv/65536;
  int b = y + Cb*bu/65536;
  target[i] = byte4(clamp(0,b,255), clamp(0,g,255), clamp(0,r,255));
 }
 return target;
}

struct Background : Widget {
 string baseName = arguments()[0];
 int2 size;

 size_t clipIndex = 1, clipFrameIndex = 0, clipFrameCount = 0;
 Map clip[3];

 size_t frameIndex = 0, frameCount = 0;

 //float R = 16;
 //float e = 16;//./256;

 ImageF mode[3];

 unique<Window> window = nullptr;
 bool toggle = false;

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
  ImageF YUV[] {toFloat(this->image(0)), toFloat(upsample(this->image(1))), toFloat(upsample(this->image(2)))};
  if(!YUV[0]) { requestTermination(); return shared<Graphics>(); }

  /*ImageF linear = ::linear(source);
  //ImageF blur = unsafeRef(linear);
  ImageF blur = gaussianBlur(linear, this->R);
  //ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b); });
  //ImageF mask = apply(mode, blur, [](float a, float b){ return sq(a-b); });
  ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b) > 1./16; });*/

  const int R = toggle ? 16 : 32;
  const float e = 256;
  const ImageF& I = YUV[0];
  ImageF meanI = ::mean(I, R);
  ImageF corrII = ::mean(::sq(I), R);
  ImageF covII = apply(meanI, corrII, [](float meanI, float corrII){ return corrII - meanI*meanI; });

  for(size_t i: range(3)) {
   const ImageF& p = YUV[i];
   ImageF meanP = ::mean(p, R);
   ImageF corrIP = ::mean(::mul(I, p), R);
   ImageF covIP = apply(corrIP, meanI, meanP, [](float corrIP, float meanI, float meanP){ return corrIP - meanI*meanP; });
   ImageF a = apply(covII, covIP, [e](float covII, float covIP){ return covIP/(covII+e); });
   ImageF b = apply(meanP, a, meanI, [](float meanP, float a, float meanI){ return meanP - a*meanI; });
   ImageF meanA = ::mean(a, R);
   ImageF meanB = ::mean(b, R);
   ImageF q = apply(meanA, I, meanB, [](float meanA, float I, float meanB){ return meanA*I + meanB; });
   //ImageF target = apply(q, I, [](float q, float I) { return I*q; });
   YUV[i] = ::move(q);
  }
  resize(window->target, sRGB709(YUV[0], YUV[1], YUV[2]));
  clipFrameIndex++;
  frameIndex++;
  return shared<Graphics>();
 }
} app;
#endif
