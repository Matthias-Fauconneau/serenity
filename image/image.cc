#include "image.h"

// -- sRGB --

void linear(mref<float> target, ref<byte4> source, int component) {
 /***/ if(component==0) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[0]]; }, source);
 else if(component==1) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[1]]; }, source);
 else if(component==2) target.apply([](byte4 sRGB) { return sRGB_reverse[sRGB[2]]; }, source);
 else error(component);
}

uint8 sRGB(float v) {
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
