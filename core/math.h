#pragma once
/// \file math.h Floating-point builtins
#include "core.h"

/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

// -- Generic arithmetic
generic inline constexpr T sq(const T x) { return x*x; }
generic inline constexpr T cb(const T x) { return x*x*x; }

inline float abs(float x) { return __builtin_fabsf(x); }
inline double abs(double x) { return __builtin_fabs(x); }

constexpr float nanf = __builtin_nanf("");
constexpr double nan = __builtin_nan("");
inline bool isNaN(float x) { return x!=x; }
inline bool isNaN(double x) { return x!=x; }
static constexpr float inf = __builtin_inff();
inline bool isNumber(float x) { return !isNaN(x) && x !=inf && x !=-inf; }

inline float floor(float x) { return __builtin_floorf(x); }
inline double floor(double x) { return __builtin_floor(x); }
inline float fract(float x) { return x - floor(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline double round(double x) { return __builtin_round(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline double ceil(double x) { return __builtin_ceil(x); }
inline double mod(double q, double d) { return __builtin_fmod(q, d); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline double sqrt(double f) { return __builtin_sqrt(f); }
inline double pow(double x, double y) { return __builtin_pow(x,y); }

const double expUnderflow = -7.45133219101941108420e+02;
const double expOverflow = 7.09782712893383973096e+02;
inline double exp(double x) { assert(x>expUnderflow && x<expOverflow); return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }

constexpr double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline float cos(float t) { return __builtin_cos(t); }
inline double acos(double t) { return __builtin_acos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline float sin(float t) { return __builtin_sin(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double tan(double t) { return __builtin_tan(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
//inline float atan(float y, float x) { return __builtin_atan2f(y, x); }
inline float atan(float y, float x) {
   static constexpr float c1 = PI/4, c2 = 3*c1;
   float abs_y = abs(y); //+1e-10 // kludge to prevent 0/0 condition
   float angle;
   if(x>=0) { float r = (x - abs_y) / (x + abs_y); angle = c1 - c1 * r; }
   else { float r = (x + abs_y) / (abs_y - x); angle = c2 - c1 * r; }
   if(y < 0) return -angle; else return angle;
}

inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }

inline double exp2(double x) { return __builtin_exp2(x); }
inline double log2(double x) { return __builtin_log2(x); }
inline double exp10(double x) { return __builtin_exp2(__builtin_log2(10)*x); }
inline double log10(double x) { return __builtin_log10(x); }

constexpr float pow4(float x) { return x*x*x*x; }
constexpr float pow5(float x) { return x*x*x*x*x; }

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

generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }

inline float mean(const ref<float> v) { return sum(v, 0.)/v.size; }
