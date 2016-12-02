#include "image.h"

// -- Resample (3x8bit) --

void box(const Image& target, const Image& source) {
 //assert_(!source.alpha); //FIXME: not alpha correct
 //assert_(source.size.x/target.size.x == source.size.y/target.size.y, target, source, source.size.x/target.size.x, source.size.y/target.size.y);
 int scale = min(source.size.x/target.size.x, source.size.y/target.size.y);
 assert_(scale <= 512, target.size, source.size);
 assert_((target.size-int2(1))*scale+int2(scale-1) < source.size, target, source);
 assert_(source.stride>1 && target.stride>1, source.stride, target.stride);
 log(target.size, target.stride, target.ref::size, source.size, source.stride, source.ref::size);
 for(size_t y : range(target.height)) {
  const byte4* sourceLine = source.data + y * scale * source.stride;
  byte4* targetLine = target.begin() + y * target.stride;
  for(uint unused x: range(target.width)) {
   const byte4* sourceSpanOrigin = sourceLine + x * scale;
   uint4 sum = 0;
   for(uint i: range(scale)) {
    const byte4* sourceSpan = sourceSpanOrigin + i * source.stride;
    for(uint j: range(scale)) {
     uint4 s (sourceSpan[j]);
     s.b = s.b*s.a; s.g = s.g*s.a; s.r = s.r*s.a;
     sum += uint4(s);
    }
   }
   if(sum.a) { sum.b = sum.b / sum.a; sum.g = sum.g / sum.a; sum.r = sum.r / sum.a; }
   sum.a /= scale*scale;
   targetLine[x] = byte4(sum[0], sum[1], sum[2], sum[3]);
  }
 }
}

void bilinear(const Image& target, const Image& source) {
 //assert_(!source.alpha, source.size, target.size);
 assert_(source.stride>1 && target.stride>1);
 const uint stride = source.stride;
 for(size_t y: range(target.height)) {
  for(uint x: range(target.width)) {
   const uint64 fx = (uint64)x*256*(source.width-1)/target.width, fy = (uint64)y*256*(source.height-1)/target.height; //TODO: incremental
   uint ix = fx/256, iy = fy/256;
   uint u = fx%256, v = fy%256;
   const ref<byte4> span = source.slice((uint64)iy*stride+ix);
   byte4 d = 0;
   uint a  = ((uint(span[      0][3]) * (256-u) + uint(span[           1][3])  * u) * (256-v)
     + (uint(span[stride][3]) * (256-u) + uint(span[stride+1][3]) * u) * (       v) ) / (256*256);
   if(a) for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
    d[i] = ((uint(span[      0][3]) * uint(span[      0][i]) * (256-u) + uint(span[           1][3]) * uint(span[           1][i]) * u) * (256-v)
      + (uint(span[stride][3]) * uint(span[stride][i]) * (256-u) + uint(span[stride+1][3]) * uint(span[stride+1][i]) * u) * (       v) )
      / (a*256*256);
   }
   d[3] = a;
   target(x, y) = d;
  }
 }
}

/// Resizes \a source into \a target
void resize(const Image& target, const Image& source) {
 assert_(source && target && source.size != target.size, source, target);
 if(source.width%target.width==0 && source.height%target.height==0) box(target, source); // Integer box downsample
 else if(target.size > source.size/2) bilinear(target, source); // Bilinear resample
 else { // Integer box downsample + Bilinear resample
  int downsampleFactor = min(source.size.x/target.size.x, source.size.y/target.size.y);
  assert_(downsampleFactor, target, source);
  bilinear(target, box(Image((source.size)/downsampleFactor, source.stride/downsampleFactor, source.alpha), source));
 }
}
