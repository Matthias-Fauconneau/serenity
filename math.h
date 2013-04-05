#pragma once
typedef double real;

/*inline float floor(float x) { return __builtin_floorf(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }*/

inline real floor(real x) { return __builtin_floor(x); }
inline real round(real x) { return __builtin_round(x); }
inline real ceil(real x) { return __builtin_ceil(x); }
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
inline real sqrt(real f) { return __builtin_sqrt(f); }
inline real pow(real x, real y) { return __builtin_pow(x,y); }

inline real exp2(real x) { return __builtin_exp2(x); }
inline real log2(real x) { return __builtin_log2(x); }
inline real exp(real x) { return __builtin_exp(x); }
inline real ln(real x) { return __builtin_log(x); }
inline real exp10(real x) { return exp(x*ln(10)); }
inline real log10(real x) { return __builtin_log10(x); }

inline real cos(real t) { return __builtin_cos(t); }
inline real acos(real t) { return __builtin_acos(t); }
inline real sin(real t) { return __builtin_sin(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }

const real PI = 3.14159265358979323846;
inline real rad(real t) { return t/180*PI; }
inline real deg(real t) { return t/PI*180; }
