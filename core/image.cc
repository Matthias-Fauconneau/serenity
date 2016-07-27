#include "image.h"

// -- sRGB --

inline double pow(double x, double y) { return __builtin_pow(x,y); } // math.h
uint8 sRGB_forward[0x1000]; // 4K (FIXME: interpolation of a smaller table might be faster)
__attribute((constructor(1001))) void generate_sRGB_forward() {
 for(uint index: range(sizeof(sRGB_forward))) {
  double linear = (double) index / (sizeof(sRGB_forward)-1);
  double sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
  assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))< 0x1p-50);
  sRGB_forward[index] = round(0xFF*sRGB);
 }
}
