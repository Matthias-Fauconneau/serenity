#include "image.h"
#include "srgb.h"
#include "algorithm.h"
#include "parallel.h"
#include "simd.h"
#include "time.h"

// -- sRGB --

void linear(mref<float> target, ref<byte4> source, int component) {
 /***/ if(component==0) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[0]]; }, source);
 else if(component==1) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[1]]; }, source);
 else if(component==2) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[2]]; }, source);
 else error(component);
}

uint8 sRGB(float v) {
 //assert_(isNumber(v), v);
 v = ::min(1.f, v); // Saturates
 v = ::max(0.f, v); // Clips
 uint linear12 = 0xFFF*v;
 assert_(linear12 < 0x1000);
 return sRGB_forward[linear12];
}
void sRGB(mref<byte4> target, ref<float> source) {
 target.apply([](float value) { uint8 v=sRGB(value); return byte4(v,v,v, 0xFF); }, source);
}
void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red) {
 target.apply([=](float b, float g, float r) { return byte4(sRGB(b), sRGB(g), sRGB(r), 0xFF); }, blue, green, red);
}

// -- Resample --

ImageF downsample(ImageF&& target, const ImageF& source) {
 assert_(target.size == source.size/2, target.size, source.size);
 for(uint y: range(target.height)) for(uint x: range(target.width))
  target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
 return move(target);
}
void upsample(const ImageF& target, const ImageF& source) {
 assert_(target.size/2 == source.size, target.size, source.size);
 for(uint y: range(source.height)) for(uint x: range(source.width)) {
  target(x*2+0,y*2+0) = source(x+0,y+0);
  target(x*2+1,y*2+0) = (source(x+0,y+0) + source(x+1,y+0))/2;
  target(x*2+0,y*2+1) = (source(x+0,y+0) + source(x+0,y+1))/2;
  target(x*2+1,y*2+1) = (source(x+0,y+0) + source(x+1,y+0) + source(x+0,y+1) + source(x+1,y+1))/4;
 }
 for(uint y: range(source.height*2, target.height)) for(uint x: range(target.width)) target(x, y) = 0;
 for(uint x: range(source.width*2, target.width)) for(uint y: range(target.height)) target(x, y) = 0;
}

// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
 parallel_for(height, [=](uint, size_t y) {
 //for(size_t y: range(height)) {
  const uint N = radius+1+radius;
  assert_(N <= 1024, N);
  const float* line = source + y * sourceStride;
  float* targetColumn = target + y;
  if(width >= radius+1) {
   for(int x: range(-radius,0)) {
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   assert_(width >= 2*radius);
   for(int x: range(0,width-2*radius)) {
    v4sf sum4 = 0;
    const float* span = line + x;
    //const size_t align = 8*sizeof(float);
    //assert_(uint64(span)%align==0, uint64(source)%align, uint64(line)%align, uint64(span)%align, sourceStride);
    for(uint dx=0; dx<N/4*4; dx+=4) sum4 += *(v4sf*)(kernel+dx) * loadu(span+dx); // FIXME: align
    float sum = hsum(sum4);
    for(uint dx=N/4*4; dx<N; dx++) sum += kernel[dx] * span[dx];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   for(int x: range(width-2*radius,width-radius)){
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(x+dx-(width-1))];
    targetColumn[(x+radius)*targetStride] = sum;
   }
  } else {
   for(int x: range(-radius, width-radius)) {
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(abs(x+dx)-(width-1))];
    targetColumn[(x+radius)*targetStride] = sum;
   }
  }
 });
}

inline void operator*=(mref<float> values, float factor) { values.apply([factor](float v) { return factor*v; }, values); }

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
 assert_(source.size == target.size);
 assert_(sigma > 0);
 if(!radius) radius = ::min(::min(source.size.x, source.size.y)/2, (int)ceil(3*sigma));
 const size_t N = radius+1+radius;
 if(N > 256) {
  assert_(N <= 2048, N);
  log("Approximation", N, N/2);
  ImageF downsampled = downsample(source);
  ImageF approximate = gaussianBlur(downsampled, sigma/2, ::min(::min(downsampled.size.x, downsampled.size.y)/2, (int)ceil(3*(sigma/2))));
  upsample(target, approximate);
 } else {
  assert_(int2(radius+1) <= source.size, sigma, radius, N, source.size);
  float kernel[N];
  for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
  float sum = ::sum(ref<float>(kernel,N), 0.); assert_(sum, ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
  buffer<float> transpose (source.width*/*align(8, source.height)*/source.height);
  Time time{true};
  convolve(transpose.begin(), source.begin(), kernel, radius, source.width, source.height, source.stride, /*align(8, source.height)*/source.height);
  log(time); time.reset();
  convolve(target.begin(), transpose.begin(), kernel, radius, target.height, target.width, /*align(8, source.height)*/source.height, target.stride);
  log(time);
 }
}
