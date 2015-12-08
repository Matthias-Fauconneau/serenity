#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#if __INTEL_COMPILER && __MIC__
typedef uint v16ui __attribute((__vector_size__ (64)));
typedef int v16si __attribute((__vector_size__ (64)));
inline v16ui uintX(uint x) { return (v16ui){x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x}; }
static v16ui unused _0i = uintX(0);
static v16ui unused _1i = uintX(~0);

#include <immintrin.h>

static inline v16ui gather(const uint* P, v16ui i) { return _mm512_i32gather_epi32((v16si)i, (const int*)P, sizeof(int)); }

typedef float v16sf __attribute((__vector_size__ (64)));
inline v16sf float16(float f) { return (v16sf){f,f,f,f,f,f,f,f,f,f,f,f,f,f,f,f}; }
static v16sf unused _0f = float16(0);
static v16sf unused _1f = float16(1);

static inline v16sf load(const float* a, size_t index) { return *(v16sf*)(a+index); }
static inline v16sf load(ref<float> a, size_t index) { return load(a.data, index); }
static inline v16sf loadu(ref<float> a, size_t index) { return load(a, index); }

static inline void store(float* const a, size_t index, v16sf v) { *(v16sf*)(a+index) = v; }
static inline void store(mref<float> a, size_t index, v16sf v) { store(a.begin(), index, v); }
static inline void storeu(mref<float> a, size_t index, v16sf v) { store(a, index, v); }

static inline v16sf mask3_fmadd(v16sf a, v16sf b, v16sf c, uint16 k) { return _mm512_mask3_fmadd_ps(a, b, c, k); }
static inline v16sf maskSub(v16sf a, uint16 k, v16sf b) { return _mm512_mask_sub_ps(a, k, a, b); }

static inline v16sf min(v16sf a, v16sf b) { return _mm512_min_ps(a, b); }
static inline v16sf max(v16sf a, v16sf b) { return _mm512_max_ps(a, b); }
static inline v16sf sqrt(v16sf x) { return _mm512_sqrt_ps(x); }

static inline float extract(v16sf x, int i) { union { float e[16]; v16sf v; } X; X.v = x; return X.e[i]; }

static inline v16sf gather(const float* P, v16ui i) { return _mm512_i32gather_ps((v16si)i, P, sizeof(float)); }

static inline void scatter(float* const P, const v16ui i, const v16sf x) { _mm512_i32scatter_ps(P, (v16si)i, x, sizeof(float)); }

static inline uint16 greaterThan(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_GT_OS); }
static inline uint16 equal(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_EQ_OQ); }

static inline v16sf blend(uint16 k, v16sf a, v16sf b) { return _mm512_mask_blend_ps(k, a, b); }

static constexpr size_t simd = 16; // SIMD size
typedef v16sf vXsf;
typedef v16ui vXui;
typedef uint16 maskX;
inline vXsf floatX(float x) { return float16(x); }

#else

typedef uint v8ui __attribute((__vector_size__ (32)));
typedef int v8si __attribute((__vector_size__ (32)));
inline v8ui uintX(uint x) { return (v8ui){x,x,x,x,x,x,x,x}; }
static v8ui unused _0i = uintX(0);
static v8ui unused _1i = uintX(~0);

#if __INTEL_COMPILER
static inline float extract(v8ui x, int i) { union { uint e[8]; v8ui v; } X; X.v = x; return X.e[i]; }
#include <immintrin.h>
#else
static inline float extract(v8ui x, int i) { return x[i]; }
#endif

static inline v8ui gather(const uint* P, v8ui i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_d256(_0i, (const v8si*)P, i, _1i, sizeof(int));
#elif __INTEL_COMPILER
 return _mm256_i32gather_epi32((const int*)P, (v8si)i, sizeof(int));
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

typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static v8sf unused _0f = float8(0);
static v8sf unused _1f = float8(1);

static inline v8sf load(const float* a, size_t index) { return *(v8sf*)(a+index); }
static inline v8sf load(ref<float> a, size_t index) { return load(a.data, index); }
static inline v8sf loadu(ref<float> a, size_t index) {
 struct v8sfu { v8sf v; } __attribute((__packed__, may_alias));
 return ((v8sfu*)(a.data+index))->v;
}

static inline void store(float* const a, size_t index, v8sf v) { *(v8sf*)(a+index) = v; }
static inline void store(mref<float> a, size_t index, v8sf v) { store(a.begin(), index, v); }
static inline void storeu(mref<float> a, size_t index, v8sf v) { __builtin_ia32_storeups256(a.begin()+index, v); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }

#if __INTEL_COMPILER
static inline float extract(v8sf x, int i) { union { float e[8]; v8sf v; } X; X.v = x; return X.e[i]; }
#else
static inline float extract(v8sf x, int i) { return x[i]; }
#endif
static inline v8sf gather(const float* P, v8ui i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_ps256(_0f, (const v8sf*)P, i, _1i, sizeof(float));
#elif __INTEL_COMPILER
 return _mm256_i32gather_ps(P, (v8si)i, sizeof(float));
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

#if __INTEL_COMPILER
static inline v8ui greaterThan(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_GT_OS); }
static inline v8ui notEqual(v8ui a, v8ui b) { return (v8ui)(v8sf)_mm256_cmp_ps((v8sf)a, (v8sf)b, _CMP_NEQ_UQ); }
static inline v8ui equal(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_EQ_OQ); }
#else
static inline v8ui greaterThan(v8sf a, v8sf b) { return a > b; }
static inline v8ui notEqual(v8ui a, v8ui b) { return a != b; }
static inline v8ui equal(v8sf a, v8sf b) { return a == b; }
#endif

static const unused v8ui select {1<<7, 1<<6, 1<<5, 1<<4,  1<<3, 1<<2, 1<<1, 1<<0};
static inline v8ui expandMask(const uint8 mask) { return notEqual(uintX(mask) & select, _0i); }

#if __clang__
static inline v8sf blend(v8ui k, v8sf a, v8sf b) { return __builtin_ia32_blendvps256(a, b, k); }
#else
static inline v8sf blend(v8ui k, v8sf a, v8sf b) { return __builtin_ia32_blendvps256(a, b, (v8sf)k); }
#endif
static inline v8sf blend(uint8 k, v8sf a, v8sf b) { return blend(expandMask(k), a, b); }

static inline v8sf mask(v8ui a, v8sf b) { return (v8sf)(a & (v8ui)b); }
static inline v8sf maskSub(v8sf a, v8ui k, v8sf b) { return a - mask(k, b); }
static inline v8sf mask3_fmadd(v8sf a, v8sf b, v8sf c, v8ui k) { return mask(k, a * b) + c; }

static constexpr size_t simd = 8; // SIMD size
typedef v8sf vXsf;
typedef v8ui vXui;
typedef v8ui maskX;
inline vXsf floatX(float x) { return float8(x); }

#endif
