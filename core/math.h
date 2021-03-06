#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// -- Generic arithmetic
generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline bool isNaN(float x) { return x!=x; }
static constexpr float inff = __builtin_inff();
inline bool isNumber(float x) { return !isNaN(x) && x != __builtin_inff() && x != -__builtin_inff(); }

#include <cmath>
/*inline float abs(float x) noexcept { return __builtin_fabsf(x); }
inline float floor(float x) noexcept { return __builtin_floorf(x); }
inline float round(float x) noexcept { return __builtin_roundf(x); }
inline double round(double x) { return __builtin_round(x); }
inline float ceil(float x) noexcept { return __builtin_ceilf(x); }
inline float sqrt(float f) noexcept { return __builtin_sqrtf(f); }
inline float cos(float t) noexcept { return __builtin_cosf(t); }
inline float acos(float t) noexcept { return __builtin_acosf(t); }
inline float sin(float t) noexcept { return __builtin_sinf(t); }
inline double pow(double x, double y) noexcept { return __builtin_pow(x,y); }*/

inline float fract(float x) noexcept { return x - floor(x); }
inline float rsqrt(float f) noexcept { return 1/__builtin_sqrtf(f); }

constexpr float PI = 3.14159265358979323846;
inline float atan(float y, float x) noexcept { return __builtin_atan2f(y, x); }

