#include "window.h"

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
#include "png.h"

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

void downsample(const Image8& target, const Image8& source) {
 assert_(target.size == source.size/2, target.size, source.size);
 for(uint y: range(target.height)) for(uint x: range(target.width))
  target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
}
Image8 downsample(const Image8& source) { Image8 target(source.size/2); downsample(target, source); return target; }

/*Image8 upsample(const Image8& source) {
 size_t w=source.width, h=source.height;
 Image8 target(w*2,h*2);
 for(size_t y: range(h)) for(size_t x: range(w)) {
  target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
 }
 return target;
}*/

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

void mean(const ImageF& target, const ImageF& buffer, const ImageF& source, int R) {
 assert(target.size == buffer.size.yx() && buffer.size.yx() == source.size);
 for(size_t unused i: range(2)) {
  const ImageF& X = *(const ImageF*[]){&source, &buffer}[i];
  const ImageF& Y = *(const ImageF*[]){&buffer, &target}[i];
  for(size_t y: range(X.size.y)) {
   float sum = 0;
   for(size_t x: range(R)) sum += X(x, y);
   for(size_t x: range(R)) {
    sum += X(x+R, y);
    Y(y, x) = sum / (R+x+1);
   }
   for(size_t x: range(R, X.size.x-R)) {
    sum += X(x+R, y);
    Y(y, x) = sum / (2*R+1);
    sum -= X(x-R, y);
   }
   for(size_t x: range(X.size.x-R, X.size.x)) {
    Y(y, x) = sum / (R+(X.size.x-1-x)+1);
    sum -= X(x-R, y);
   }
  }
 }
}

const double Kb = 0.0722, Kr = 0.2126;
const double rv = (1-Kr)*255/112;
const double gu = (1-Kb)*Kb/(1-Kr-Kb)*255/112;
const double gv = (1-Kr)*Kr/(1-Kr-Kb)*255/112;
const double bu = (1-Kb)*255/112;

void sRGBfromBT709(const Image& target, const ImageF& Y, const ImageF& U, const ImageF& V) {
 for(size_t i: range(Y.ref::size)) {
  int y = (int(Y[i]) - 16)*255/219;
  int Cb = int(U[i]) - 128;
  int Cr = int(V[i]) - 128;
  int r = y                        + Cr*rv;
  int g = y - Cb*gu - Cr*gv;
  int b = y + Cb*bu;
  target[i] = byte4(clamp(0,b,255), clamp(0,g,255), clamp(0,r,255));
 }
}

#if 0
Image sRGBfromBT709(const Image8& Y, const Image8& U, const Image8& V) {
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
#endif

map<string, Time> times;
void guidedFilter(const Image& target, const Image8& Y, const Image8& U, const Image8& V) {
 times["toFloat"].start();
 assert(Y.size == U.size && Y.size == V.size);
 ImageF I[] {toFloat(Y), toFloat(U), toFloat(V)};
 int2 size = I[0].size;
 times["toFloat"].stop();

 /*ImageF linear = ::linear(source);
 //ImageF blur = unsafeRef(linear);
 ImageF blur = gaussianBlur(linear, this->R);
 //ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b); });
 //ImageF mask = apply(mode, blur, [](float a, float b){ return sq(a-b); });
 ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b) > 1./16; });*/

 const int R = 16; // 16
 const float e = 256;
 ImageF buffer {size.yx()};
 ImageF meanI[3] {size, size, size};
 ImageF corrII[6] {size, size, size, size, size, size};
 size_t index[3*3] = {0,1,2, 1,3,4, 2,4,5};

 for(size_t i: range(3)) { // meanI, covII
  times["meanI"].start();
  ::mean(meanI[i], buffer, I[i], R);
  times["meanI"].stop();
  times["corrII"].start();
  for(size_t j: range(i+1)) {
   const ImageF& corrIIij = corrII[index[i*3+j]];
   for(size_t k: range(corrIIij.ref::size)) corrIIij[k] = I[i][k] * I[j][k]; // corrIIij
   ::mean(corrIIij, buffer, corrIIij, R); // -> mean corrIIij
  }
  times["corrII"].stop();
 }

 ref<ImageF> p (I);
 ImageF meanP[3] {size, size, size};
 ImageF corrIP[3*3] {size,size,size, size,size,size, size,size,size};
 ImageF a[3*3] {size,size,size, size,size,size, size,size,size};
 const ref<ImageF> b (meanP);

 for(size_t i: range(3)) {
 times["meanP"].start();
 ::mean(meanP[i], buffer, p[i], R);
 times["meanP"].stop();
 times["corrIP"].start();
 for(size_t j: range(3)) {
  const ImageF& corrIPij = corrIP[i*3+j];
  for(size_t k: range(corrIPij.ref::size)) corrIPij[k] =  p[i][k] * I[j][k]; // corrIPij
  ::mean(corrIPij, buffer, corrIPij, R); // -> mean corrIPij
 }
 times["corrIP"].stop();
 }
 times["ab"].start();
 for(size_t k: range(p.ref::size)) {
  float m00 = corrII[0][k] - meanI[0][k]*meanI[0][k], m01 = corrII[1][k] - meanI[0][k]*meanI[1][k], m02 = corrII[2][k] - meanI[0][k]*meanI[2][k];
  float m11 = corrII[3][k] - meanI[1][k]*meanI[1][k], m12 = corrII[4][k] - meanI[1][k]*meanI[2][k];
  float m22 = corrII[5][k] - meanI[2][k]*meanI[2][k];

  float D = 1/(- m00*m01*m01 + m00*m11*m22
               - m01*m01*m22 + 2*m01*m02*m12
               - m02*m02*m11);

  float a00 = (m22*m11 - m12*m12) * D + e;
  float a01 = (m02*m12 - m22*m01) * D;
  float a02 = (m01*m12 - m02*m11) * D;

  float a11 = (m00*m22 - m02*m02) * D + e;
  float a12 = (m01*m02 - m00*m12) * D;

  float a22 = (m00*m11 - m01*m01) * D + e;

  for(size_t i: range(3)) {
   float meanPi = meanP[i][k];
   float meanI0 = meanI[0][k];
   float meanI1 = meanI[1][k];
   float meanI2 = meanI[2][k];
   float covIPi0 = corrIP[i*3+0][k]  - meanPi*meanI0;
   float covIPi1 = corrIP[i*3+1][k]  - meanPi*meanI1;
   float covIPi2 = corrIP[i*3+2][k]  - meanPi*meanI2;

   float ai0 = a00*covIPi0 + a01*covIPi1 + a02*covIPi2;
   a[i*3+0][k] = ai0;
   float ai1 = a01*covIPi0 + a11*covIPi1 + a12*covIPi2;
   a[i*3+1][k] = ai1;
   float ai2 = a02*covIPi0 + a12*covIPi1 + a22*covIPi2;
   a[i*3+2][k] = ai2;
   b[i][k] = meanPi - (ai0 * meanI0 + ai1 * meanI1 + ai2 * meanI2);
  }
 }
 times["ab"].stop();
 ref<ImageF> meanA (a);
 ref<ImageF> meanB (b);
 ref<ImageF> q (meanB);
 for(size_t i: range(3)) {
  times["meanA"].start();
  for(size_t j: range(3)) ::mean(meanA[i*3+j], buffer, a[i*3+j], R); // -> meanA
  times["meanA"].stop();
  times["meanB"].start();
  ::mean(meanB[i], buffer, b[i], R); // -> meanB
  times["meanB"].stop();
  times["q"].start();
  for(size_t k: range(q.ref::size)) q[i][k] = meanB[i][k] + (meanA[i*3+0][k] * I[0][k] + meanA[i*3+1][k] * I[1][k] + meanA[i*3+2][k] * I[2][k]);
  times["q"].stop();
 }
 times["sRGB"].start();
 sRGBfromBT709(target, q[0], q[1], q[2]);
 times["sRGB"].stop();
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
  Image target(size);
  totalTime.reset();
  guidedFilter(target, images[0], images[1], images[2]);
  uint64 t = totalTime.reset();
  log(apply(times, [t](const Time& time) { return strD(time, t); }),
      strD(sum(apply(ref<Time>(times.values), [](const Time& t) { return (uint64)t; })), t), str(t/second, 1u)+"s");

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

 vec2 sizeHint(vec2) override { return vec2(size); };

 shared<Graphics> graphics(vec2) override {
  Image8 images[] {downsample(this->image(0)), this->image(1), this->image(2)};
  if(images[0]) {
   Image target(images[0].size);
   guidedFilter(target, images[0], images[1], images[2]);
   resize(window->target, target);
   //uint64 t = totalTime.reset();
   //log(apply(ref<Time>(times), [t](const Time& time) { return strD(time, t); }), strD(sum(ref<Time>(times)), t), str(t/second, 1u)+"s");
   clipFrameIndex++;
   frameIndex++;
  } else {
   requestTermination();
  }
  return shared<Graphics>();
 }
} app;
#endif
