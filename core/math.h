#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// -- Generic arithmetic
generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline float abs(float x) { return __builtin_fabsf(x); }
inline bool isNaN(float x) { return x!=x; }
static constexpr float inff = __builtin_inff();
inline bool isNumber(float x) { return !isNaN(x) && x != __builtin_inff() && x != -__builtin_inff(); }

inline float floor(float x) { return __builtin_floorf(x); }
inline float fract(float x) { return x - floor(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline float rsqrt(float f) { return 1/__builtin_sqrtf(f); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }

constexpr float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float acos(float t) { return __builtin_acosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float atan(float y, float x) { return __builtin_atan2f(y, x); }

inline double pow(double x, double y) { return __builtin_pow(x,y); }

// -> \file algorithm.h

template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A accumulator, ref<Ss>... sources) {
 for(size_t index: range(values.size)) accumulator = fold(accumulator, values[index], sources[index]...);
 return accumulator;
}

generic T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
generic T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }

template<Type A, Type T> T sum(ref<T> values, A initialValue) {
 return reduce(values, [](A accumulator, T value) { return accumulator + value; }, initialValue);
}
generic T sum(ref<T> values) { return sum(values, T()); }

