#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// -- Generic arithmetic
generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline float abs(float x) { return __builtin_fabsf(x); }
inline double abs(double x) { return __builtin_fabs(x); }

constexpr float nanf = __builtin_nanf("");
constexpr double nan = __builtin_nan("");
inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(double x) { return x!=x; }
static constexpr float inff = __builtin_inff();
inline bool isNumber(float x) { return !isNaN(x) && x !=inff && x !=-inff; }
static constexpr double inf = __builtin_inf();
inline bool isNumber(double x) { return !isNaN(x) && x !=inf && x !=-inf; }

inline float round(float x) { return __builtin_roundf(x); }
inline double round(double x) { return __builtin_round(x); }

#if 0
inline float floor(float x) { return __builtin_floorf(x); }
inline double floor(double x) { return __builtin_floor(x); }
inline float fract(float x) { return x - floor(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline double ceil(double x) { return __builtin_ceil(x); }
inline double mod(double q, double d) { return __builtin_fmod(q, d); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline double sqrt(double f) { return __builtin_sqrt(f); }

inline double pow(double x, double y) { return __builtin_pow(x,y); }

const double expUnderflow = -7.45133219101941108420e+02;
const double expOverflow = 7.09782712893383973096e+02;
inline double exp(double x) { assert(x>expUnderflow && x<expOverflow); return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }

constexpr double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline float cos(float t) { return __builtin_cos(t); }
inline double acos(double t) { return __builtin_acos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline float sin(float t) { return __builtin_sin(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double tan(double t) { return __builtin_tan(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
//inline float atan(float y, float x) { return __builtin_atan2f(y, x); }
/*inline float atan(float y, float x) {
   static constexpr float c1 = PI/4, c2 = 3*c1;
   float abs_y = abs(y); //+1e-10 // kludge to prevent 0/0 condition
   float angle;
   if(x>=0) { float r = (x - abs_y) / (x + abs_y); angle = c1 - c1 * r; }
   else { float r = (x + abs_y) / (abs_y - x); angle = c2 - c1 * r; }
   if(y < 0) return -angle; else return angle;
}*/

inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }

inline double exp2(double x) { return __builtin_exp2(x); }
inline double log2(double x) { return __builtin_log2(x); }
inline double exp10(double x) { return __builtin_exp2(__builtin_log2(10)*x); }
inline double log10(double x) { return __builtin_log10(x); }

constexpr float pow4(float x) { return x*x*x*x; }
constexpr float pow5(float x) { return x*x*x*x*x; }
#endif
