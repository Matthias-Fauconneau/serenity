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
  for(size_t y: range(cropY, image.height)) for(size_t x: range(cropX, image.width)) {
   const uint v = image(x,y);
   min = ::min<uint>(min, v);
   max = ::max<uint>(max, v);
  }

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
   float factor = (maxWB * (max-min)) / (whiteBalance[i] * ((1<<15)-1) * sum); // Also normalizes max-min to 15bit
   for(int j: range(3)) sensor_RGB(i, j) = sensor_RGB(i, j) * factor;
  }
  mat3 RGB_sensor = sensor_RGB.inverse();
  mat3 XYZ_sensor = XYZ_RGB*RGB_sensor; // FIXME: direct WB normalization in XYZ ?
  /*for(float c: RGB_sensor.data) assert_(c < 1<<5);
  constexpr int shift = 31-13-5; float scale = 1<<shift;
  int32 m00 = scale*RGB_sensor(0,0), m01 = scale*RGB_sensor(0, 1), m02 = scale*RGB_sensor(0, 2); // R
  int32 m10 = scale*RGB_sensor(1,0), m11 = scale*RGB_sensor(1, 1), m12 = scale*RGB_sensor(1, 2); // G
  int32 m20 = scale*RGB_sensor(2,0), m21 = scale*RGB_sensor(2, 1), m22 = scale*RGB_sensor(2, 2); // B
  int32 m30 = XYZ_RGB(1, 0)*(m00+m10+m20), m31 = XYZ_RGB(1, 0)*(m01+m11+m21), m32 = XYZ_RGB(1, 0)*(m02+m12+m22); // Y*/
  buffer<uint32> histogram (1<<14); // 128K
  histogram.clear(0);
  int2 size ((image.width-cropX)/2, (image.height-cropY)/2);
  //Image16 R(size), G(size), B(size);
  uint maxY = 0;
  for(size_t tY: range(size.y)) {
   for(size_t tX: range(size.x)) {
    size_t y = cropY+2*tY, x = cropX+2*tX; // crop/2*2
    int32 cR = image(x+0,y+0)-min;
    int32 cG = image(x+1,y+0)+image(x+0,y+1)-2*min;
    int32 cB = image(x+1,y+1)-min;
    assert_(cR>=0 && cR<1<<13 && cG>=0 && cG<1<<13 && cB>=0 && cB<1<<13, cR, cG, cB, tX, tY, x, y);
    /*int r = (m00 * cR + m01 * cG + m02 * cB)>>shift;
    int g = (m10 * cR + m11 * cG + m12 * cB)>>shift;
    int b = (m20 * cR + m21 * cG + m22 * cB)>>shift;
    int Y = (m30 * cR + m31 * cG + m32 * cB)>>shift;
    assert_(r>=0 && r<1<<15 && g>=0 && g<1<<15 && b>=0 && b<1<<15, r, g, b);*/
    int Y = (XYZ_sensor * vec3(cR, cG, cB))[1];
    maxY = ::max<uint>(maxY, Y);
    assert_(Y >= 0 && Y < 1<<14, Y);
    histogram[Y]++;
    /*R(tX, tY) = r;
    G(tX, tY) = g;
    B(tX, tY) = b;*/
   }
  }

  buffer<uint16> HE (maxY+1); // 128K
  const uint totalCount = size.x*size.y;
  const uint targetMaxY = 1<<12;
  for(uint bin=0, count=0; bin < maxY; bin++) {
   HE[bin] = (uint64)count*targetMaxY/totalCount;
   count += histogram[bin];
  }

  Image16 R(size), G(size), B(size);
  mat3 RGB_XYZ = XYZ_RGB.inverse();
  for(size_t tY: range(size.y)) {
   for(size_t tX: range(size.x)) {
    size_t y = cropY+2*tY, x = cropX+2*tX; // crop/2*2
    int32 cR = image(x+0,y+0)-min;
    int32 cG = image(x+1,y+0)+image(x+0,y+1)-2*min;
    int32 cB = image(x+1,y+1)-min;
    vec3 XYZ = XYZ_sensor * vec3(cR, cG, cB);
    int32 Y = XYZ.y;
    int32 HEy = HE[Y];
    float HEscale = float(HEy)/float(Y);
    rgb3f RGB = RGB_XYZ * (HEscale * XYZ);
    R(tX, tY) = RGB.r;
    G(tX, tY) = RGB.g;
    B(tX, tY) = RGB.b;
   }
  }

#if 0
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
