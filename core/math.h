#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// -- Generic arithmetic
generic inline constexpr T cb(const T x) { return x*x*x; }

/// Returns the largest positive integer that divides the numbers without a remainder
inline int gcd(int a, int b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }

inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(double x) { return x!=x; }
inline bool isNumber(float x) { return !isNaN(x) && x != __builtin_inff() && x != -__builtin_inff(); }
inline bool isNumber(double x) { return !isNaN(x) && x != __builtin_inf() && x != -__builtin_inf(); }

inline float floor(float x) noexcept { return __builtin_floorf(x); }
//inline double floor(double x) noexcept { return __builtin_floor(x); }
inline float round(float x) noexcept { return __builtin_roundf(x); }
//inline double round(double x) noexcept { return __builtin_round(x); }
inline float ceil(float x) noexcept { return __builtin_ceilf(x); }
//inline double ceil(double x) noexcept { return __builtin_ceil(x); }
inline float fract(float x) noexcept { return x - floor(x); }

inline float sqrt(float f) noexcept { return __builtin_sqrtf(f); }
//inline double sqrt(double f) noexcept { return __builtin_sqrt(f); }
inline float rsqrt(float f) noexcept { return 1/__builtin_sqrtf(f); }

inline float cos(float t) noexcept { return __builtin_cosf(t); }
inline float acos(float t) noexcept { return __builtin_acosf(t); }
inline float sin(float t) noexcept { return __builtin_sinf(t); }

//inline double pow(double x, double y) noexcept { return __builtin_pow(x,y); }

//inline double exp(double x) { return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }

constexpr float π = 3.14159265358979323846f;
inline float atan(float y, float x) noexcept { return __builtin_atan2f(y, x); }

//inline double tan(double t) { return __builtin_tan(t); }
inline float tan(float t) { return __builtin_tanf(t); }
