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
  for(size_t y: range(cropY, image.height-cropY)) for(size_t x: range(cropX, image.width-cropX)) {
   const uint v = image(x,y);
   min = ::min<uint>(min, v);
   max = ::max<uint>(max, v);
  }

  mat3 camXYZ {
    vec3( 0.9602, -0.2984, -0.0407),
    vec3(-0.3823,  1.1495,  0.1415),
    vec3(-0.0937,  0.1675,  0.5049)};
  mat3 XYZrgb {
    vec3(0.4124, 0.2126, 0.0193),
    vec3(0.3576, 0.7152, 0.1192),
    vec3(0.1805, 0.0722, 0.9505) };
  mat3 camRGB = camXYZ*XYZrgb;
  for(int i: range(3)) { // Normalizes so that camRGB * 1 is 1
   float sum = 0;
   for(int j: range(3)) sum += camRGB(i, j);
   log(sum);
   for(int j: range(3)) camRGB(i, j) /= sum;
  }
  uint maxBalance = ::min(::max(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
  vec3 whiteBalance ( (float)cr2.whiteBalance.R/maxBalance, (float)cr2.whiteBalance.G/maxBalance/2, (float)cr2.whiteBalance.B/maxBalance );
  mat3 m = camRGB.inverse() * mat3(whiteBalance*(((1<<15)-1)/float(int(max-min))));
  constexpr int shift = 31-15-7; float scale = 1<<shift;
  for(float c: m.data) assert_(c < 1<<7);
  int32 m00 = scale*m(0,0), m01 = scale*m(0, 1), m02 = scale*m(0, 2); // R
  int32 m10 = scale*m(1,0), m11 = scale*m(1, 1), m12 = scale*m(1, 2); // G
  int32 m20 = scale*m(2,0), m21 = scale*m(2, 1), m22 = scale*m(2, 2); // B
  int32 m30 = XYZrgb(1, 0)*(m00+m10+m20), m31 = XYZrgb(1, 0)*(m01+m11+m21), m32 = XYZrgb(1, 0)*(m02+m12+m22); // Y
  buffer<uint32> histogram (1<<15); // 128K
  histogram.clear(0);
  int2 size ((image.width-cropX)/2, (image.height-cropY)/2);
  Image16 R(size), G(size), B(size);
  for(size_t tY: range(size.y)) {
   for(size_t tX: range(size.x)) {
    size_t y = cropY+2*tY, x = cropX+2*tX;
    int32 cR = image(x+0,y+0)-min;
    int32 cG = image(x+1,y+0)+image(x+0,y+1)-2*min;
    int32 cB = image(x+1,y+1)-min;
    int r = (m00 * cR + m01 * cG + m02 * cB)>>shift;
    int g = (m10 * cR + m11 * cG + m12 * cB)>>shift;
    int b = (m20 * cR + m21 * cG + m22 * cB)>>shift;
    int Y = (m30 * cR + m31 * cG + m32 * cB)>>shift;
    assert_(r>=-(1<<15) && r<1<<15 && b>=-(1<<15) && b<1<<15 && g>=-(1<<15) && g<1<<15, r, g, b);
    assert_(Y >= -6 && Y < 1<<15, Y);
    histogram[::max(0,Y)]++;
    R(tX, tY) = r;
    G(tX, tY) = g;
    B(tX, tY) = b;
   }
  }
  uint bin = 0; for(uint count =0; count < size_t(size.x*size.y/2); bin++) count += histogram[bin];

#if 1
  // Lens distortion correction
  Image16 Ru(size), Gu(size), Bu(size);
  assert_(cr2.focalLengthMM==8.8f, cr2.focalLengthMM);
  const float a = 0.03872, b = -0.15563, c = 0.02544, d = 1-a-b-c;
  vec2 center = vec2(size-int2(1))/2.f;
  for(size_t Uy: range(size.y)) {
   for(size_t Ux: range(size.x)) {
    vec2 U = vec2(Ux, Uy) - center;
    float Ur = length(U) / (size.y/2);
    float Dr = (((a*Ur + b)*Ur+ c)*Ur + d)*Ur;
    vec2 D = center + (Dr/Ur)*U;
    int Dx = round(D.x), Dy = round(D.y);
    assert_(Dx >= 0 && Dx < size.x && Dy >= 0 && Dy < size.y, U, Ur, Dr, D, size);
    // Nearest resample
    Ru(Ux, Uy) = R(Dx, Dy);
    Gu(Ux, Uy) = G(Dx, Dy);
    Bu(Ux, Uy) = B(Dx, Dy);
   }
  }
  R = ::move(Ru);
  G = ::move(Gu);
  B = ::move(Bu);
#endif

  Image sRGB (size);
  for(size_t i: range(sRGB.ref::size)) {
   // Scales median to -4bit
   uint r = (uint(R[i]) << (12-4)) / bin;
   uint g = (uint(G[i]) << (12-4)) / bin;
   uint b = (uint(B[i]) << (12-4)) / bin;
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
