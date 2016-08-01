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

  const int minCropX = 96, minCropY = 18;
  const int fullX = image.size.x-minCropX, fullY = image.size.y-minCropY;
  constexpr int N = 256;
  const int cropX = minCropX+fullX-(fullX/(3*2*N)*(3*2*N)), cropY = minCropY+fullY-(fullY/(2*2*N)*(2*2*N));
  assert_(cropX >= minCropX && cropX <= fullX/5 && cropY >= minCropY && cropY <= fullY/5, fullX, fullY, cropX, cropY, fullX/cropX, fullY/cropY);

  // Min/Max
  uint min = -1, max = 0;
  for(size_t y: range(cropY, image.size.y)) for(size_t x: range(cropX, image.size.x)) {
   const uint v = image(x,y);
   min = ::min<uint>(min, v);
   max = ::max<uint>(max, v);
  }

  // "Demosaic" (G1+G2) (and de-bias black level)
  int2 size ((image.size.x-cropX)/2, (image.size.y-cropY)/2);
  Image16 R(size), G(size), B(size);
  const size_t bStride = image.stride, stride = R.stride;
  for(size_t y: range(size.y)) {
   const size_t bRowI = (cropY+2*y)*bStride + cropX; // crop/2*2
   const size_t rowI = y*stride;
   for(size_t x: range(size.x)) {
    const size_t bI = bRowI + 2*x;
    const int32 r = image[bI]-min;
    const int32 g = image[bI+1]+image[bI+bStride]-2*min;
    const int32 b = image[bI+bStride+1]-min;
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
  mat3 sensor_RGB = sensor_XYZ*XYZ_RGB;
  // White balance factors maps sensor RGBs to WB-corrected RGBs so that (1,1,1)_WB = (1,0,1)_XYZ (i.e (1,1,1)_RGB)
  rgb3 whiteBalance = cr2.whiteBalance;
  whiteBalance.g /= 2; // G1+G2
  int maxWB = ::max(::max(whiteBalance.r, whiteBalance.g), whiteBalance.b);
  for(int i: range(3)) { // (1,1,1)_WB = (1,1,1)_RGB
   float sum = 0;
   for(int j: range(3)) sum += sensor_RGB(i, j);
   float factor = (maxWB * (max-min)) / (whiteBalance[i] * ((1<<16)-1) * sum); // Also normalizes max-min to 15bit
   for(int j: range(3)) sensor_RGB(i, j) = sensor_RGB(i, j) * factor;
  }
  mat3 RGB_sensor = sensor_RGB.inverse();
  mat3 XYZ_sensor = XYZ_RGB*RGB_sensor; // FIXME: direct WB normalization in XYZ ?

#if 0
  // Luminance histogram
  buffer<uint32> histogram (1<<15); // 128K
  histogram.clear(0);
  uint maxY = 0;
  for(size_t i: range(R.ref::size)) {
   int Y = (XYZ_sensor * vec3(R[i], G[i], B[i]))[1];
   maxY = ::max<uint>(maxY, Y);
   assert_(Y >= 0 && Y < 1<<15, Y);
   histogram[Y]++;
  }

  // Evaluates histogram equalization
  buffer<uint16> HE (maxY+1); // 128K
  const uint totalCount = size.x*size.y;
  const uint targetMaxY = 1<<12;
  for(uint bin=0, count=0; bin < maxY; bin++) {
   HE[bin] = (uint64)count*targetMaxY/totalCount;
   count += histogram[bin];
  }

  // Colorspace conversion, Histogram equalization
  mat3 RGB_XYZ = XYZ_RGB.inverse();
  for(size_t i: range(R.ref::size)) {
   vec3 XYZ = XYZ_sensor * vec3(R[i], G[i], B[i]);
   int32 Y = XYZ.y;
   int32 HEy = HE[Y];
   float HEscale = float(HEy)/float(Y);
   rgb3f RGB = RGB_XYZ * (HEscale * XYZ);
   assert_(int3(vec3(RGB))<int3(8192), RGB);
   R[i] = int(RGB.r);
   G[i] = int(RGB.g);
   B[i] = int(RGB.b);
  }
#elif 0
  // Luminance image
  uint maxY = 0;
  Image16 Y(size);
  for(size_t i: range(R.ref::size)) {
   int y = (XYZ_sensor * vec3(R[i], G[i], B[i]))[1];
   maxY = ::max<uint>(maxY, y);
   assert_(y >= 0 && y < 1<<15, y);
   Y[i] = y;
  }

  // Colorspace conversion, Adaptive histogram equalization
  mat3 RGB_XYZ = XYZ_RGB.inverse();
  log("AHE");
  Time AHE {true};
  for(int y: range(Y.size.y)) {
   for(int x: range(Y.size.x)) {
    const int L = 8;
    int s = 0;
    size_t  i = y*Y.stride+x;
    int r = Y[i];
    for(int dy: range(::max(0, y-L), ::min(Y.size.y, y+L+1))) {
     for(int dx: range(::max(0, x-L), ::min(Y.size.x, x+L+1))) {
      int c = Y(dx, dy);
      if(c < r) s += 2;
      else if(c==r) s += 1;
     }
    }
    assert_(s < sq(2*L+1)*2, s, sq(2*L+1)*2);
    vec3 XYZ = XYZ_sensor * vec3(R[i], G[i], B[i]);
    int32 y = XYZ.y;
    int32 HEy = s*(1<<11)/(sq(2*L+1)*2);
    float HEscale = float(HEy)/float(y);
    rgb3f RGB = RGB_XYZ * (HEscale * XYZ);
    assert_(int3(vec3(RGB))<int3(8192), RGB);
    R[i] = int(RGB.r);
    G[i] = int(RGB.g);
    B[i] = int(RGB.b);
   }
  }
  log(AHE);
#else
  assert_(2*size.x == 3*size.y && size.x%N == 0 && size.y%N == 0, size,  size.x%N, size.y%N, image.size, cropX, cropY);
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
  mat3 RGB_XYZ = XYZ_RGB.inverse();
  for(size_t Y: range(size.y/N-1)) {
   for(size_t X: range(size.x/N-1)) {
    const size_t tileI = (Y*N+N/2)*stride + (X*N+N/2);
    const HE HE00 = HEs(X+0, Y+0);
    const HE HE10 = HEs(X+1, Y+0);
    const HE HE01 = HEs(X+0, Y+1);
    const HE HE11 = HEs(X+1, Y+1);
    for(int y: range(N)) {
     const size_t rowI = tileI + y*stride;
     const float fy = float(y)/N;
     for(int x: range(N)) {
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
      assert_(int3(vec3(RGB))<int3(8192), RGB, HEy, HEscale, fx, fy, HE00y, HE10y, HE01y, HE11y);
      R[i] = int(RGB.r);
      G[i] = int(RGB.g);
      B[i] = int(RGB.b);
     }
    }
   }
  }
#endif

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
