#include "cr2.h"
#include "matrix.h"
#include "time.h"
#include "png.h"
inline double log2(double x) { return __builtin_log2(x); } // math.h
inline int floor(int W, int v) { return v/W*W; }
inline int2 floor(int2 W, int2 v) { return int2(floor(W.x, v.x), floor(W.y, v.y)); }

struct Convert {
 Convert() {
  string name = arguments()[0];
  CR2 cr2 = CR2( Map(name) );
  const Image16& image = cr2.image;

  constexpr int2 minOffset (48, 9);
  constexpr int Ncrop = 128;
  constexpr int N = 0;
  int2 size = floor(int2(3,2)*Ncrop, image.size/2-minOffset);
  int2 offset = minOffset+((image.size/2-minOffset)-size)/2; // Centers crop

  // Min/Max
  uint minB = -1, maxB = 0;
  {const int stride = image.stride;
   for(size_t y: range(size.y*2)) {
    const size_t rowI = (offset.y*2 + y)*stride + offset.x*2;
    for(size_t x: range(size.x*2)) {
     const uint v = image[rowI + x];
     minB = ::min<uint>(minB, v);
     maxB = ::max<uint>(maxB, v);
    }
   }
  }

  // "Demosaic" (G1+G2) (also crops and de-bias black level)
  Image16 R(size), G(size), B(size);
  const int bStride = image.stride, stride = R.stride;
  for(size_t y: range(size.y)) {
   const size_t bRowI = (offset.y+y)*2*bStride + offset.x*2;
   const size_t rowI = y*stride;
   for(size_t x: range(size.x)) {
    const size_t bI = bRowI + 2*x;
    const int32 r = image[bI]-minB;
    const int32 g = image[bI+1]+image[bI+bStride]-2*minB;
    const int32 b = image[bI+bStride+1]-minB;
    assert_(r>=0 && r<1<<13 && g>=0 && g<1<<13 && b>=0 && b<1<<13, r, g, b, x, y);
    const size_t i = rowI + x;
    R[i] = r;
    G[i] = g;
    B[i] = b;
   }
  }

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
    // Nearest resample (TODO: linear)
    Ru(Ux, Uy) = R(Dx, Dy);
    Gu(Ux, Uy) = G(Dx, Dy);
    Bu(Ux, Uy) = B(Dx, Dy);
   }
  }
  R = ::move(Ru);
  G = ::move(Gu);
  B = ::move(Bu);

  mat3 sensor_XYZ {
   vec3( 0.9602, -0.2984, -0.0407),
   vec3(-0.3823,  1.1495,  0.1415),
   vec3(-0.0937,  0.1675,  0.5049)};
  mat3 XYZ_RGB {
   vec3(0.4124, 0.2126, 0.0193),
   vec3(0.3576, 0.7152, 0.1192),
   vec3(0.1805, 0.0722, 0.9505) };
  const mat3 RGB_XYZ = XYZ_RGB.inverse();
#if 1
  mat3 sensor_RGB = sensor_XYZ*XYZ_RGB;
  // White balance factors
  rgb3 whiteBalance = cr2.whiteBalance;
  whiteBalance.g /= 2; // G1+G2
  int maxWB = ::max(::max(whiteBalance.r, whiteBalance.g), whiteBalance.b);
  for(int i: range(3)) { // 1_RGB' = WB_RGB
   float sum = 0;
   for(int j: range(3)) sum += sensor_RGB(i, j);
   float factor = (maxWB * (maxB-minB)) / (whiteBalance[i] * ((1<<16)-1) * sum); // Also normalizes max-min to 16bit
   for(int j: range(3)) sensor_RGB(i, j) = sensor_RGB(i, j) * factor;
  }
  mat3 RGB_sensor = sensor_RGB.inverse();
  mat3 XYZ_sensor = XYZ_RGB*RGB_sensor;
#else // FIXME
  // White balance factors maps sensor RGBs to WB-corrected RGBs so that (1,1,1)_WB = (1/3,1,1/3)_XYZ
  rgb3 whiteBalance = cr2.whiteBalance;
  whiteBalance.g /= 2; // G1+G2
  int maxWB = ::max(::max(whiteBalance.r, whiteBalance.g), whiteBalance.b);
  for(int i: range(3)) { // (1,1,1)_WB = (1/3,1,1/3)_XYZ
   float sum = 0;
   for(int j: range(3)) sum += sensor_XYZ(i, j);
   float factor = (maxWB * (max-min) * vec3(0.3127, 1, 0.3290)[i]) / (whiteBalance[i] * ((1<<16)-1) * sum); // Also normalizes max-min to 16bit
   for(int j: range(3)) sensor_XYZ(i, j) = sensor_XYZ(i, j) * factor;
  }
  mat3 XYZ_sensor = sensor_XYZ.inverse();
  mat3 RGB_sensor = RGB_XYZ * XYZ_sensor;
#endif

  if(N) {
   assert_(2*size.x == 3*size.y && size.x%N == 0 && size.y%N == 0, size, size.x%N, size.y%N, image.size, offset);
   const int maxY = 1<<15;
   struct HE { uint16 HE[maxY]; };
   ImageT<HE> HEs (size/N); // 20 MB
   for(size_t Y: range(size.y/N)) {
    for(size_t X: range(size.x/N)) {
     // Luminance histogram
     buffer<uint16> histogram (maxY); // 64K
     histogram.clear(0);
     const size_t tileI = Y*N*stride + X*N;
     for(int y: range(N)) {
      const size_t rowI = tileI + y*stride;
      for(int x: range(N)) {
       const size_t i = rowI + x;
       int Y = (XYZ_sensor * vec3(R[i], G[i], B[i]))[1];
       assert_(Y >= 0 && Y < maxY, Y);
       histogram[Y]++;
      }
     }

     // Evaluates histogram equalization
     HE& HE = HEs(X, Y);
     constexpr uint totalCount = N*N;
     constexpr uint targetMaxY = 1<<11;
     for(uint bin=0, count=0; bin < maxY; bin++) {
      HE.HE[bin] = (uint64)count*targetMaxY/totalCount;
      count += histogram[bin];
     }
    }
   }

   // Colorspace conversion, Histogram equalization
   const int W = size.x/N, H = size.y/N;
   for(int Y: range(H+1)) {
    for(int X: range(W+1)) {
     const int tileI = (Y*N-N/2)*stride + (X*N-N/2);
     const HE HE00 = HEs(max(0, X-1), max(0, Y-1));
     const HE HE10 = HEs(min(W-1, X), max(0, Y-1));
     const HE HE01 = HEs(max(0, X-1), min(H-1, Y));
     const HE HE11 = HEs(min(W-1, X), min(H-1, Y));
     for(int y: range(max(0, N/2-N*Y), min(N, size.y+N/2-N*Y))) {
      assert_(Y*N-N/2+y >= 0 && Y*N-N/2+y < size.y);
      const int rowI = tileI + y*stride;
      const float fy = float(y)/N;
      for(int x: range(max(0, N/2-N*X), min(N, size.x+N/2-N*X))) {
       assert_(X*N-N/2+x >= 0 && X*N-N/2+x < size.x);
       const size_t i = rowI + x;
       const vec3 XYZ = XYZ_sensor * vec3(R[i], G[i], B[i]);
       const int32 Y = XYZ.y;
       const int32 HE00y = HE00.HE[Y];
       const int32 HE01y = HE01.HE[Y];
       const int32 HE10y = HE10.HE[Y];
       const int32 HE11y = HE11.HE[Y];
       // FIXME: 5 fmul, 5 cvt -> 4 imul, 2 shift, 1 cvt ?
       const float fx = float(x)/N;
       const float HEy =
         (1-fy) * ( (1-fx) * float(HE00y) + fx * float(HE10y) ) +
         fy * ( (1-fx) * float(HE01y) + fx * float(HE11y) );
       const float HEscale = float(HEy)/XYZ.y;
       const rgb3f RGB = RGB_XYZ * (HEscale * XYZ);
       assert_(int3(vec3(RGB))<int3(8192), RGB, HEy);
       R[i] = int(RGB.r);
       G[i] = int(RGB.g);
       B[i] = int(RGB.b);
      }
     }
    }
   }
  } else {
   // Colorspace conversion
   for(size_t i: range(R.ref::size)) {
    rgb3f RGB = RGB_sensor * vec3(R[i], G[i], B[i]) / 4.f;
    assert_(int3(vec3(RGB))<int3(8192), RGB);
    R[i] = int(RGB.r);
    G[i] = int(RGB.g);
    B[i] = int(RGB.b);
   }
  }
  // Gamma compression
  Image sRGB (size);
  for(size_t i: range(sRGB.ref::size)) {
   uint r = R[i];
   uint g = G[i];
   uint b = B[i];
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
