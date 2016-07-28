#include "cr2.h"
#include "matrix.h"
#include "time.h"
#include "png.h"
inline double log2(double x) { return __builtin_log2(x); } // math.h

struct Convert {
 Convert() {
  string name = arguments()[0];
  CR2 cr2 = CR2( Map(name) );
  const Image16& image = cr2.image;
  constexpr size_t cropY = 18, cropX = 96;
  uint min = -1, max = 0;
  buffer<uint32> histogram (1<<13); // 256K
  histogram.clear(0);
  for(size_t y: range(cropY, image.height-cropY)) for(size_t x: range(cropX, image.width-cropX)) {
   const uint v = image(x,y);
   assert_(v < 1<<13, v);
   min = ::min<uint>(min, v);
   max = ::max<uint>(max, v);
   histogram[v]++;
  }
  uint bin = 0; for(uint count =0; count < size_t((image.height-2*cropY)*(image.width-2*cropX)/2); bin++) count += histogram[bin];
  assert_(bin > min);
  bin -= min;
  log(bin);

  mat3 camXYZ {
    vec3( 0.9602, -0.2984, -0.0407),
    vec3(-0.3823,  1.1495,  0.1415),
    vec3(-0.0937,  0.1675,  0.5049)};
  mat3 XYZrgb {
    vec3(0.4124, 0.2126, 0.0193),
    vec3(0.3576, 0.7152, 0.1192),
    vec3(0.1805, 0.0722, 0.9505) };
  mat3 camRGB = camXYZ*XYZrgb;
  if(1) for(int i: range(3)) { // Normalizes so that camRGB * 1 is 1
   float sum = 0;
   for(int j: range(3)) sum += camRGB(i, j);
   for(int j: range(3)) camRGB(i, j) /= sum;
  }
  //uint maxBalance = ::max(::max(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
  //vec3 whiteBalance ( (float)cr2.whiteBalance.R/maxBalance, (float)cr2.whiteBalance.G/maxBalance/2, (float)cr2.whiteBalance.B/maxBalance );
  uint minBalance = ::min(::min(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
  vec3 whiteBalance ( (float)cr2.whiteBalance.R/minBalance, (float)cr2.whiteBalance.G/minBalance/2, (float)cr2.whiteBalance.B/minBalance );
  //mat3 m = camRGB.inverse() * mat3(whiteBalance*(((1<<15)-1)/float(int(max-min))));
  log(bin, log2(bin/float(1<<14)), float(1<<10)/bin, log2(float(1<<10)/bin));
  mat3 m = camRGB.inverse() * mat3(whiteBalance*(((1<<(14-4))-1)/float(bin)));
  constexpr int shift = 31-15-7; float scale = 1<<shift;
  for(float c: m.data) assert_(c < 1<<7);
  int32 m00 = scale*m(0,0), m01 = scale*m(0, 1), m02 = scale*m(0, 2);
  int32 m10 = scale*m(1,0), m11 = scale*m(1, 1), m12 = scale*m(1, 2);
  int32 m20 = scale*m(2,0), m21 = scale*m(2, 1), m22 = scale*m(2, 2);

  int2 size ((image.width-cropX)/2, (image.height-cropY)/2);
  Image16 R(size), G(size), B(size);
  /*buffer<uint32> histogram (1<<16); // 256K
  histogram.clear(0);*/
  for(size_t Y: range(size.y)) {
   for(size_t X: range(size.x)) {
    size_t y = cropY+2*Y, x = cropX+2*X;
    int32 cR = image(x+0,y+0)-min;
    int32 cG = image(x+1,y+0)+image(x+0,y+1)-2*min;
    int32 cB = image(x+1,y+1)-min;
    //histogram[::max(0, cR+cG+cB)/3]++; // FIXME: brightness?
    int r = (m00 * cR + m01 * cG + m02 * cB)>>shift;
    int g = (m10 * cR + m11 * cG + m12 * cB)>>shift;
    int b = (m20 * cR + m21 * cG + m22 * cB)>>shift;
    //assert_(r>=0 && r<1<<15 && b>=0 && b<1<<15 && g>=0 && g<1<<15, r, g, b);
    assert_(r>=-(1<<15) && r<1<<15 && b>=-(1<<15) && b<1<<15 && g>=-(1<<15) && g<1<<15, r, g, b);
    //histogram[::max(0, r+g+b)/3]++; // FIXME: brightness?
    R(X, Y) = r;
    G(X, Y) = g;
    B(X, Y) = b;
   }
  }
  //uint bin = 0; for(uint count =0; count < size_t(size.x*size.y/2); bin++) count += histogram[bin];
  //log(bin, log2(bin/float(1<<15)), float(1<<11)/bin, log2(float(1<<11)/bin));
  Image sRGB (size);
  for(size_t i: range(sRGB.ref::size)) {
   // Scales median to -4bit
   //uint r = (uint(R[i]) << 8) / bin; // 12-4
   //uint g = (uint(G[i]) << 8) / bin;
   //uint b = (uint(B[i]) << 8) / bin;
   uint r = uint(R[i]) >> 2;
   uint g = uint(G[i]) >> 2;
   uint b = uint(B[i]) >> 2;
   sRGB[i] = byte4(
      sRGB_forward[::min(b, 0xFFFu)],
      sRGB_forward[::min(g, 0xFFFu)],
      sRGB_forward[::min(r, 0xFFFu)], 0xFF);
  }
  Time encode {true};
  buffer<byte> png = encodePNG(sRGB);
  log(encode);
  writeFile(section(name,'.')+".png"_, png, currentWorkingDirectory(), true);
 }
} app;
