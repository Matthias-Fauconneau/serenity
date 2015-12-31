#include "image.h"

// -- sRGB --

void linear(mref<float> target, ref<byte4> source, int component) {
 /***/ if(component==0) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[0]]; }, source);
 else if(component==1) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[1]]; }, source);
 else if(component==2) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[2]]; }, source);
 else error(component);
}

static uint8 sRGB(float v) {
 assert_(isNumber(v), v);
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

// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
 int N = radius+1+radius;
 assert_(N < 1024, N);
 //chunk_parallel(height, [=](uint, size_t y) {
 for(size_t y: range(height)) {
  const float* line = source + y * sourceStride;
  float* targetColumn = target + y;
  if(width >= radius+1) {
   for(int x: range(-radius,0)) {
    float sum = 0;
    for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   for(int x: range(0,width-2*radius)) {
    float sum = 0;
    const float* span = line + x;
    for(int dx: range(N)) sum += kernel[dx] * span[dx];
    targetColumn[(x+radius)*targetStride] = sum;
   }
   assert_(width >= 2*radius);
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
 }
}

inline void operator*=(mref<float> values, float factor) { values.apply([factor](float v) { return factor*v; }, values); }

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
 assert_(sigma > 0);
 if(!radius) radius = ceil(3*sigma);
 size_t N = radius+1+radius;
 assert_(int2(radius+1) <= source.size, sigma, radius, N, source.size);
 float kernel[N];
 for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
 float sum = ::sum(ref<float>(kernel,N), 0.); assert_(sum, ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
 buffer<float> transpose (target.height*target.width);
 convolve(transpose.begin(), source.begin(), kernel, radius, source.width, source.height, source.stride, source.height);
 assert_(source.size == target.size);
 convolve(target.begin(),  transpose.begin(), kernel, radius, target.height, target.width, target.height, target.stride);
}
