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

const double Kb = 0.0722, Kr = 0.2126;
const double rv = (1-Kr)*255/112;
const double gu = (1-Kb)*Kb/(1-Kr-Kb)*255/112;
const double gv = (1-Kr)*Kr/(1-Kr-Kb)*255/112;
const double bu = (1-Kb)*255/112;

void sRGB709(const Image& target, const ImageF& Y, const ImageF& U, const ImageF& V) {
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
#endif

Time times[9];
void guidedFilter(const Image& target, const Image8& Y, const Image8& U, const Image8& V) {
 times[0].start();
 ImageF I[] {toFloat(Y), toFloat(U), toFloat(V)};
 times[0].stop();

 /*ImageF linear = ::linear(source);
 //ImageF blur = unsafeRef(linear);
 ImageF blur = gaussianBlur(linear, this->R);
 //ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b); });
 //ImageF mask = apply(mode, blur, [](float a, float b){ return sq(a-b); });
 ImageF mask = apply(mode, blur, [](float a, float b){ return abs(a-b) > 1./16; });*/

 const int R = 32; // 16
 const float e = 256;
 ImageF meanI[3];
 ImageF covII[6];
 size_t index[3*3] = {0,1,2, 1,3,4, 2,4,5};

 times[1].start();
 for(size_t i: range(3)) {
  const ImageF& Ii = I[i];
  meanI[i] = ::mean(Ii, R);
  for(size_t j: range(i+1)) {
   ImageF corrIIij (I[i].size);
   for(size_t k: range(corrIIij.ref::size)) corrIIij[k] =  I[i][k] * I[j][k];
   ImageF meanCorrIIij = ::mean(corrIIij, R);
   ImageF covIIij (corrIIij.size);
   for(size_t k: range(covIIij.ref::size)) covIIij[k] = meanCorrIIij[k] - meanI[i][k]*meanI[j][k];
   covII[index[i*3+j]] = ::move(covIIij);
 }
 }
 times[1].stop();

 for(size_t i: range(3)) {
  times[2].start();
  const ImageF& p = I[i];
  ImageF meanP = ::mean(p, R);
  times[2].stop();
  times[3].start();
  ImageF covIP[3];
  for(size_t j: range(3)) {
   ImageF corrIPj (I[i].size);
   for(size_t k: range(corrIPj.ref::size)) corrIPj[k] =  p[k] * I[j][k];
   ImageF meanCorrIPij = ::mean(corrIPj, R);
   ImageF covIPj (corrIPj.size);
   for(size_t k: range(covIPj.ref::size)) covIPj[k] = meanCorrIPij[k] - meanP[k]*meanI[i][k];
   covIP[j] = ::move(covIPj);
  }
  times[3].stop();
  times[4].start();
  ImageF a[3] {p.size, p.size, p.size};
  ImageF b (p.size);
  times[4].stop();
  times[5].start();
  for(size_t k: range(p.ref::size)) {
   float m11 = covII[0][k], m12 = covII[1][k], m13 = covII[2][k];
   float m22 = covII[3][k], m23 = covII[4][k];
   float m33 = covII[5][k];

   float D = 1/(- m11*m12*m12 + m11*m22*m33
                      - m12*m12*m33 + 2*m12*m13*m23
                      - m13*m13*m22);

   float a11 = (m33*m22 - m23*m23) * D + e;
   float a12 = (m13*m23 - m33*m12) * D;
   float a13 = (m12*m23 - m13*m22) * D;

   float a22 = (m11*m33 - m13*m13) * D + e;
   float a23 = (m12*m13 - m11*m23) * D;

   float a33 = (m11*m22 - m12*m12) * D + e;

   a[0][k] = a11*covIP[0][k] + a12*covIP[1][k] + a13*covIP[2][k];
   a[1][k] = a12*covIP[0][k] + a22*covIP[1][k] + a23*covIP[2][k];
   a[2][k] = a13*covIP[0][k] + a23*covIP[1][k] + a33*covIP[2][k];
   b[k] = meanP[i] - (a[0][k] * meanI[0][k] + a[1][k] * meanI[1][k] + a[2][k] * meanI[2][k]);
  }
  times[5].stop();
  times[6].stop();
  ImageF meanA[3];
  for(size_t j: range(3)) meanA[j] = ::mean(a[j], R);
  times[6].stop();
  times[7].start();
  ImageF meanB = ::mean(b, R);
  times[7].stop();
  times[8].stop();
  ImageF q (p.size);
  for(size_t k: range(q.ref::size)) {
   q[k] = meanB[i] + (meanA[0][k] * I[0][k] + meanA[1][k] * I[1][k] + meanA[2][k] * I[2][k]);
  }
  times[8].stop();
  //ImageF target = apply(q, I, [](float q, float I) { return I*q; });
  I[i] = ::move(q);
 }
 times[2].start();
 sRGB709(target, I[0], I[1], I[2]);
 times[2].stop();
 log(times);
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

  Image8 images[] {this->image(0), this->image(1), this->image(2)};
  Image target(size);
  guidedFilter(target, images[0], images[1], images[2]);
  if(1) {
   writeFile("guided.png", encodePNG(target),currentWorkingDirectory(), true);
   log(totalTime);
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
  Image8 images[] {this->image(0), this->image(1), this->image(2)};
  if(images[0]) {
   Image target(images[0].size);
   guidedFilter(target, images[0], images[1], images[2]);
   resize(window->target, target);
   log(totalTime.reset());
   clipFrameIndex++;
   frameIndex++;
  } else {
   requestTermination();
  }
  return shared<Graphics>();
 }
} app;
#endif
