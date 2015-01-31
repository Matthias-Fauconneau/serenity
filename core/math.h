#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

// -- Generic arithmetic
generic inline constexpr T sq(const T& x) { return x*x; }
generic inline constexpr T cb(const T& x) { return x*x*x; }

/// Declares real as a double-precision floating point number
typedef double real;

notrace inline float abs(float x) { return __builtin_fabsf(x); }
notrace inline real abs(real x) { return __builtin_fabs(x); }

constexpr real nan = __builtin_nan("");
inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(real x) { return x!=x; }
const float inf = __builtin_inff();
inline bool isNumber(float x) { return !isNaN(x) && x !=inf && x !=-inf; }

inline float floor(float x) { return __builtin_floorf(x); }
inline real floor(real x) { return __builtin_floor(x); }
notrace inline float fract(float x) { return x - floor(x); }
notrace inline float round(float x) { return __builtin_roundf(x); }
notrace inline real round(real x) { return __builtin_round(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline real ceil(real x) { return __builtin_ceil(x); }
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline real sqrt(real f) { return __builtin_sqrt(f); }
inline real pow(real x, real y) { return __builtin_pow(x,y); }

const real expUnderflow = -7.45133219101941108420e+02;
const real expOverflow = 7.09782712893383973096e+02;
inline real exp(real x) { assert(x>expUnderflow && x<expOverflow); return __builtin_exp(x); }
inline real ln(real x) { return __builtin_log(x); }

constexpr real PI = 3.14159265358979323846;
inline real cos(real t) { return __builtin_cos(t); }
inline float cos(float t) { return __builtin_cos(t); }
inline real acos(real t) { return __builtin_acos(t); }
notrace inline real sin(real t) { return __builtin_sin(t); }
inline float sin(float t) { return __builtin_sin(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }
inline real sinh(real x) { return __builtin_sinh(x); }

inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }

inline real exp2(real x) { return __builtin_exp2(x); }
inline real log2(real x) { return __builtin_log2(x); }
inline real exp10(real x) { return __builtin_exp2(__builtin_log2(10)*x); }
inline real log10(real x) { return __builtin_log10(x); }

// -> \file algorithm.h

template<Type A, Type F> A reduce(range range, F fold, A accumulator) {
	for(size_t index: range) accumulator = fold(accumulator, index);
	return accumulator;
}

template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A accumulator, ref<Ss>... sources) {
	for(size_t index: range(values.size)) accumulator = fold(accumulator, values[index], sources[index]...);
	return accumulator;
}
template<Type A, Type T, Type F, size_t N> T reduce(const T (&values)[N], F fold, A initialValue) {
	return reduce(ref<T>(values), fold, initialValue);
}

template<Type A, Type T> T sum(ref<T> values, A initialValue) {
	return reduce(values, [](A accumulator, T value) { return accumulator + value; }, initialValue);
}
template<Type T> T sum(ref<T> values) { return sum(values, T()); }
template<Type T, size_t N> T sum(const T (&values)[N]) { return sum(ref<T>(values)); }

template<Type T> T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
template<Type T, size_t N> T min(const T (&a)[N]) { return min(ref<T>(a)); }

template<Type T> T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }
template<Type T, size_t N> T max(const T (&a)[N]) { return max(ref<T>(a)); }

//generic uint argmin(const ref<T>& a) { uint min=0; for(uint i: range(a.size)) if(a[i] < a[min]) min=i; return min; }
generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }
