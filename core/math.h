#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

generic T abs(T x) { return x>=0 ? x : -x; }

generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline float abs(float x) noexcept { return __builtin_fabsf(x); }
inline double abs(double x) { return __builtin_fabs(x); }

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

inline float exp(float x) { return __builtin_expf(x); }
inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }
