#include "cr2.h"
#include "matrix.h"
#include "time.h"
#include "png.h"

struct Convert {
 Convert() {
  string name = arguments()[0];
  CR2 cr2 = CR2( Map(name) );
  const Image16& image = cr2.image;
  constexpr size_t cropY = 18, cropX = 96;
  int max = 4000;
  int min = max;
  for(size_t y: range(cropY, (image.height-cropY))) for(size_t x: range(cropX, (image.width-cropX))) {
   min = ::min<int>(min, image(x,y));
   max = ::max<int>(max, image(x,y));
  }

  mat3 camXYZ {
    vec3( 0.9602, -0.2984, -0.0407),
    vec3(-0.3823,  1.1495,  0.1415),
    vec3(-0.0937,  0.1675,  0.5049)};
  mat3 XYZrgb {
    vec3(0.4124, 0.2126, 0.0193),
    vec3(0.3576, 0.7152, 0.1192),
    vec3(0.1805, 0.0722, 0.9505) };
  uint minBalance = ::min(::min(cr2.whiteBalance.R, cr2.whiteBalance.G), cr2.whiteBalance.B);
  vec3 whiteBalance ( (float)cr2.whiteBalance.R/minBalance, (float)cr2.whiteBalance.G/minBalance, (float)cr2.whiteBalance.B/minBalance );
  mat3 camRGB = camXYZ*XYZrgb;
  for(int i: range(3)) { // Normalizes so that camRGB * 1 is 1
   float sum = 0;
   for(int j: range(3)) sum += camRGB(i, j);
   for(int j: range(3)) camRGB(i, j) /= sum;
  }
  mat3 m = mat3(whiteBalance) * camRGB.inverse() * mat3(vec3(1,1./2,1));
  for(float c: m.data) assert_(c < (1<<(31-12-12)));
  int32 m00 = 0xFFF*m(0,0), m01 = 0xFFF*m(0, 1), m02 = 0xFFF*m(0, 2);
  int32 m10 = 0xFFF*m(1,0), m11 = 0xFFF*m(1, 1), m12 = 0xFFF*m(1, 2);
  int32 m20 = 0xFFF*m(2,0), m21 = 0xFFF*m(2, 1), m22 = 0xFFF*m(2, 2);

  int2 size ((image.width-cropX)/2, (image.height-cropY)/2);
  Image sRGB (size);
  for(size_t Y: range(size.y)) {
   for(size_t X: range(size.x)) {
    size_t y = cropY+2*Y, x = cropX+2*X;
    int r = image(x+0,y+0), g1 = image(x+1,y+0), g2 = image(x+0,y+1), b = image(x+1,y+1);
    int cR = r-min;
    int cG = g1+g2-2*min;
    int cB = b-min;
    int R = ::clamp(0, (m00 * cR + m01 * cG + m02 * cB)/(max-min), 0xFFF);
    int G = ::clamp(0, (m10 * cR + m11 * cG + m12 * cB)/(max-min), 0xFFF);
    int B = ::clamp(0, (m20 * cR + m21 * cG + m22 * cB)/(max-min), 0xFFF);
    sRGB(X, Y) = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
   }
  }
  Time encode {true};
  buffer<byte> png = encodePNG(sRGB);
  log(encode);
  writeFile(section(name,'.')+".png"_, png, currentWorkingDirectory(), true);
 }
} app;
