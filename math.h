#pragma once
/*inline float floor(float x) { return __builtin_floorf(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }*/
inline double floor(double x) { return __builtin_floor(x); }
inline double round(double x) { return __builtin_round(x); }
inline double ceil(double x) { return __builtin_ceil(x); }
inline double mod(double q, double d) { return __builtin_fmod(q, d); }
inline double sqrt(double f) { return __builtin_sqrt(f); }
inline double pow(double x, double y) { return __builtin_pow(x,y); }
inline double exp2(double x) { return __builtin_exp2(x); }
inline double log2(double x) { return __builtin_log2(x); }
inline double exp(double x) { return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }
inline double exp10(double x) { return exp(x*ln(10)); }
inline double log10(double x) { return __builtin_log10(x); }
inline double cos(double t) { return __builtin_cos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
const double PI = 3.14159265358979323846;
