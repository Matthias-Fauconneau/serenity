#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

/// Declares real as a double-precision floating point number
typedef double real;

constexpr real nan = __builtin_nan("");
inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(real x) { return x!=x; }
inline bool isNumber(float x) { return !isNaN(x) && x !=__builtin_inff() && x !=-__builtin_inff(); }

inline float floor(float x) { return __builtin_floorf(x); }
inline real floor(real x) { return __builtin_floor(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline real round(real x) { return __builtin_round(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline real ceil(real x) { return __builtin_ceil(x); }
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
inline constexpr float sqrt(float f) { return __builtin_sqrtf(f); }
inline real sqrt(real f) { return __builtin_sqrt(f); }
inline real pow(real x, real y) { return __builtin_pow(x,y); }

const real e = 2.71828;
const real expUnderflow = -7.45133219101941108420e+02;
const real expOverflow = 7.09782712893383973096e+02;
inline real exp(real x) { assert(x>expUnderflow && x<expOverflow); return __builtin_exp(x); }
inline real ln(real x) { return __builtin_log(x); }

inline real cos(real t) { return __builtin_cos(t); }
inline real acos(real t) { return __builtin_acos(t); }
inline real sin(real t) { return __builtin_sin(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }
inline real sinh(real x) { return __builtin_sinh(x); }

const real PI = 3.14159265358979323846;
inline real rad(real t) { return t/180*PI; }
inline real deg(real t) { return t/PI*180; }
inline real exp2(real x) { return __builtin_exp2(x); }
inline real log2(real x) { return __builtin_log2(x); }
inline real exp10(real x) { return __builtin_exp10(x); }
inline real log10(real x) { return __builtin_log10(x); }
