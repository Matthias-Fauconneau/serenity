#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// ref<Arithmetic> operations
generic const T& min(const ref<T>& a) { const T* min=&a.first(); for(const T& e: a) if(e < *min) min=&e; return *min; }
generic const T& max(const ref<T>& a) { const T* max=&a.first(); for(const T& e: a) if(*max < e) max=&e; return *max; }
generic T sum(const ref<T>& a) { T sum=0; for(const T& e: a) sum += e; return sum; }

// Arithmetic functions
generic T abs(T x) { return x>=0 ? x : -x; }
generic T sign(T x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
generic inline constexpr T sq(const T& x) { return x*x; }
generic inline constexpr T cb(const T& x) { return x*x*x; }

/// Declares real as a double-precision floating point number
typedef double real;

constexpr real nan = __builtin_nan("");
inline bool isNaN(real x) { return x!=x; }
const real inf = __builtin_inf();
inline bool isNumber(real x) { return !isNaN(x) && x !=inf && x != -inf; }

inline real floor(real x) { return __builtin_floor(x); }
inline real round(real x) { return __builtin_round(x); }
inline real ceil(real x) { return __builtin_ceil(x); }
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
inline real sqrt(real f) { return __builtin_sqrt(f); }
inline real pow(real x, real y) { return __builtin_pow(x,y); }

const real e = 2.71828;
const real expUnderflow = -7.45133219101941108420e+02;
const real expOverflow = 7.09782712893383973096e+02;
inline real exp(real x) { return __builtin_exp(x); }
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
inline real exp10(real x) { return __builtin_exp2(__builtin_log2(10)*x); }
inline real log10(real x) { return __builtin_log10(x); }
