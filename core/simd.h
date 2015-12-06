#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef uint v8ui __attribute((__vector_size__ (32)));
typedef int v8si __attribute((__vector_size__ (32)));
static /*constexpr*/ v8ui unused _0i = (v8ui){0,0,0,0,0,0,0,0};
static /*constexpr*/ v8ui unused _1i = (v8ui){
  uint(~0),uint(~0),uint(~0),uint(~0),
  uint(~0),uint(~0),uint(~0),uint(~0)};

static inline v8ui gather(const uint* P, v8ui i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_epi256(_0i, (const v8sf*)P, i, _1i, sizeof(float));
#else
 return (v8ui)__builtin_ia32_gathersiv8si((v8si)_0i, (const int*)P, (v8si)i, (v8si)_1i, sizeof(int));
#endif
#elif __INTEL_COMPILER
 union { uint e[8]; v8ui v; } I; I.v = i;
 return (v8ui){P[I.e[0]], P[I.e[1]], P[I.e[2]], P[I.e[3]], P[I.e[4]], P[I.e[5]], P[I.e[6]], P[I.e[7]]};
#else
 return (v8ui){P[i[0]], P[i[1]], P[i[2]], P[i[3]], P[i[4]], P[i[5]], P[i[6]], P[i[7]]};
#endif
}
static inline v8ui gather(ref<uint> P, v8ui i) { return gather(P.data, i); }

#if __INTEL_COMPILER && 0
union  __declspec(align(32)) __declspec(intrin_type) v8sf { float e[8]; };
#else
typedef float v8sf __attribute((__vector_size__ (32)));
#endif
inline v8sf /*constexpr*/ float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static /*constexpr*/ v8sf unused _0f = float8(0);
static /*constexpr*/ v8sf unused _1f = float8(1);

static inline v8sf load(const float* a, size_t index) { return *(v8sf*)(a+index); }
static inline v8sf load(ref<float> a, size_t index) { return load(a.data, index); }

static inline v8sf loadu(ref<float> a, size_t index) {
 struct v8sfu { v8sf v; } __attribute((__packed__, may_alias));
 return ((v8sfu*)(a.data+index))->v;
}

static inline void store(float* const a, size_t index, v8sf v) { *(v8sf*)(a+index) = v; }
static inline void store(mref<float> a, size_t index, v8sf v) { store(a.begin(), index, v); }

static inline void storeu(mref<float> a, size_t index, v8sf v) {
 __builtin_ia32_storeups256(a.begin()+index, v);
}

static inline v8sf /*operator&*/mask(v8ui a, v8sf b) { return (v8sf)(a & (v8ui)b); }
static inline v8sf /*operator|*/merge(v8sf a, v8sf b) { return (v8sf)((v8ui)a | (v8ui)b); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }

#if __INTEL_COMPILER
static inline float extract(v8sf x, int i) {  union { uint e[8]; v8sf v; } X; X.v = x; return X.e[i]; }
#else
static inline float extract(v8sf x, int i) { return x[i]; }
#endif
static inline v8sf gather(const float* P, v8ui i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_ps256(_0f, (const v8sf*)P, i, _1i, sizeof(float));
#else
 return __builtin_ia32_gathersiv8sf(_0f, P, (v8si)i, (v8sf)_1i, sizeof(float));
#endif
#elif __INTEL_COMPILER
 union { uint e[8]; v8ui v; } I; I.v = i;
 return (v8sf){P[I.e[0]], P[I.e[1]], P[I.e[2]], P[I.e[3]], P[I.e[4]], P[I.e[5]], P[I.e[6]], P[I.e[7]]};
#else
 return (v8sf){P[i[0]], P[i[1]], P[i[2]], P[i[3]], P[i[4]], P[i[5]], P[i[6]], P[i[7]]};
#endif
}
static inline v8sf gather(ref<float> P, v8ui i) { return gather(P.data, i); }

static inline void scatter(float* const P, const v8ui i, const v8sf x) {
#if __INTEL_COMPILER
 union { uint e[8]; v8ui v; } I; I.v = i;
 union { float e[8]; v8sf v; } X; X.v = x;
 P[I.e[0]] = X.e[0]; P[I.e[1]] = X.e[1]; P[I.e[2]] = X.e[2]; P[I.e[3]] = X.e[3];
 P[I.e[4]] = X.e[4]; P[I.e[5]] = X.e[5]; P[I.e[6]] = X.e[6]; P[I.e[7]] = X.e[7];
#else
 P[i[0]] = x[0]; P[i[1]] = x[1]; P[i[2]] = x[2]; P[i[3]] = x[3];
 P[i[4]] = x[4]; P[i[5]] = x[5]; P[i[6]] = x[6]; P[i[7]] = x[7];
#endif
}
static inline void scatter(mref<float> P, const v8ui a, const v8sf x) {
 scatter(P.begin(), a, x);
}

#if __INTEL_COMPILER
#include <immintrin.h>
static inline v8ui greaterThan(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_GT_OS); }
static inline v8ui notEqual(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_NEQ_UQ); }
#else
static inline v8sf greaterThan(v8sf a, v8sf b) { return a > b; }
static inline v8sf notEqual(v8sf a, v8sf b) { return a != b; }
#endif

static constexpr size_t simd = 8; // SIMD size
