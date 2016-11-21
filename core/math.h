#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline float abs(float x) { return __builtin_fabsf(x); }
inline double abs(double x) { return __builtin_fabs(x); }

//constexpr float nanf = __builtin_nanf("");
//constexpr double nan = __builtin_nan("");

inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(double x) { return x!=x; }

static constexpr float inff = __builtin_inff();
static constexpr double inf = __builtin_inf();

inline bool isNumber(float x) { return !isNaN(x) && x !=inff && x !=-inff; }
inline bool isNumber(double x) { return !isNaN(x) && x !=inf && x !=-inf; }

inline float floor(float x) { return __builtin_floorf(x); }
inline double floor(double x) { return __builtin_floor(x); }

inline float round(float x) { return __builtin_roundf(x); }
inline double round(double x) { return __builtin_round(x); }

inline float ceil(float x) { return __builtin_ceilf(x); }
inline double ceil(double x) { return __builtin_ceil(x); }

inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline double sqrt(double f) { return __builtin_sqrt(f); }

constexpr double PI = 3.14159265358979323846;

inline float sin(float t) { return __builtin_sinf(t); }
inline double sin(double t) { return __builtin_sin(t); }

inline float cos(float t) { return __builtin_cosf(t); }
inline double cos(double t) { return __builtin_cos(t); }

#if 0
inline float fract(float x) { return x - floor(x); }
inline double fract(double x) { return x - floor(x); }

inline double mod(double q, double d) { return __builtin_fmod(q, d); }
inline double pow(double x, double y) { return __builtin_pow(x,y); }

inline double exp(double x) { return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }

inline double acos(double t) { return __builtin_acos(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double tan(double t) { return __builtin_tan(t); }

inline float atan(float y, float x) { return __builtin_atan2f(y, x); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }

inline double exp2(double x) { return __builtin_exp2(x); }
inline double log2(double x) { return __builtin_log2(x); }
inline double exp10(double x) { return __builtin_exp2(__builtin_log2(10)*x); }
inline double log10(double x) { return __builtin_log10(x); }

constexpr float pow4(float x) { return x*x*x*x; }
constexpr float pow5(float x) { return x*x*x*x*x; }
#endif
