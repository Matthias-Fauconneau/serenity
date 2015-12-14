#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#if __INTEL_COMPILER && __MIC__
typedef uint v16ui __attribute((__vector_size__ (64)));
typedef int v16si __attribute((__vector_size__ (64)));
inline v16ui uintX(uint x) { return (v16ui){x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x}; }
static v16ui unused _0i = uintX(0);
static v16ui unused _1i = uintX(~0);
static v16ui unused _seqi {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

#include <immintrin.h>

static inline v16ui gather(const uint* P, v16ui i) { return _mm512_i32gather_epi32((v16si)i, (const int*)P, sizeof(int)); }

static inline v16ui min(v16ui a, v16ui b) { return _mm512_min_epi32(a, b); }
static inline v16ui max(v16ui a, v16ui b) { return _mm512_max_epi32(a, b); }
static inline uint min(v16ui x) { return _mm512_reduce_min_epi32(x); }
static inline uint max(v16ui x) { return _mm512_reduce_max_epi32(x); }

typedef float v16sf __attribute((__vector_size__ (64)));
inline v16sf float16(float f) { return (v16sf){f,f,f,f,f,f,f,f,f,f,f,f,f,f,f,f}; }
static v16sf unused _0f = float16(0);
static v16sf unused _1f = float16(1);

static inline v16sf load(const float* a, int index) { return *(v16sf*)(a+index); }
static inline v16sf load(ref<float> a, int index) { return load(a.data, index); }
static inline v16sf loadu(const float* a, int index) { return load(a, index); }
static inline v16sf loadu(ref<float> a, int index) { return load(a, index); }

static inline void store(float* const a, int index, v16sf v) { *(v16sf*)(a+index) = v; }
static inline void store(mref<float> a, int index, v16sf v) { store(a.begin(), index, v); }
static inline void storeu(float* const a, int index, v16sf v) { store(a, index, v); }
static inline void storeu(mref<float> a, int index, v16sf v) { store(a, index, v); }

static inline v16sf mask3_fmadd(v16sf a, v16sf b, v16sf c, uint16 k) { return _mm512_mask3_fmadd_ps(a, b, c, k); }
static inline v16sf maskSub(v16sf a, uint16 k, v16sf b) { return _mm512_mask_sub_ps(a, k, a, b); }

static inline v16sf min(v16sf a, v16sf b) { return _mm512_min_ps(a, b); }
static inline v16sf max(v16sf a, v16sf b) { return _mm512_max_ps(a, b); }
static inline v16sf sqrt(v16sf x) { return _mm512_sqrt_ps(x); }

static inline uint extract(v16ui x, int i) { union { uint e[16]; v16ui v; } X; X.v = x; return X.e[i]; }
static inline float extract(v16sf x, int i) { union { float e[16]; v16sf v; } X; X.v = x; return X.e[i]; }

static inline v16sf gather(const float* P, v16ui i) { return _mm512_i32gather_ps((v16si)i, P, sizeof(float)); }

static inline void scatter(float* const P, const v16ui i, const v16sf x) { _mm512_i32scatter_ps(P, (v16si)i, x, sizeof(float)); }

static inline uint16 lessThan(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_LT_OS); }
static inline uint16 greaterThan(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_GT_OS); }
static inline uint16 equal(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_EQ_OQ); }

static inline v16sf blend(uint16 k, v16sf a, v16sf b) { return _mm512_mask_blend_ps(k, a, b); }

static inline float min(v16sf x) { return _mm512_reduce_min_ps(x); }
static inline float max(v16sf x) { return _mm512_reduce_max_ps(x); }

static inline void maskStore(float* p, uint16 k, v16sf a) { _mm512_mask_store_ps(p, k, a); }
static inline void compressStore(uint* p, uint16 k, v16ui a) { _mm512_mask_compressstoreu_epi32(p, k, a); }

static constexpr int simd = 16; // SIMD size
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
static v8ui unused _seqi {0,1,2,3,4,5,6,7};

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

typedef uint v4ui __attribute((__vector_size__ (16)));
#if AVX2
static inline v8ui min(v8ui a, v8ui b) { return __builtin_ia32_pminud256(a, b); }
static inline v8ui max(v8ui a, v8ui b) { return __builtin_ia32_pmaxud256(a, b); }
#else
#if __INTEL_COMPILER
static inline v4ui low(v8ui x) { return _mm256_castsi256_si128(x); }
static inline v4ui high(v8ui x) { return _mm256_extractf128_epi32(x, 1); }
#elif __clang__
static inline v4ui low(v8ui x) { return __builtin_shufflevector(x, x, 0, 1, 2, 3); }
static inline v4ui high(v8ui x) { return __builtin_ia32_vextractf128_si256(x, 1); }
#else
static inline v4ui low(v8ui x) { return __builtin_ia32_epi32_si256(x); }
static inline v4ui high(v8ui x) { return __builtin_ia32_vextractf128_si256(x, 1); }
#endif

typedef long long m128i __attribute__((__vector_size__(16)));
typedef long long m256i __attribute__((__vector_size__(32)));
static inline v8ui uint2x4(v4ui a, v4ui b) { return __builtin_ia32_vinsertf128_si256((m256i)__builtin_shufflevector((m128i)a ,(m128i)a, 0, 1, -1, -1), b, 1); }
static inline v4ui min(v4ui a, v4ui b) { return __builtin_ia32_pminsd128(a, b); }
static inline v4ui max(v4ui a, v4ui b) { return __builtin_ia32_pmaxsd128(a, b); }
static inline v8ui min(v8ui a, v8ui b) { return uint2x4(min(low(a), high(b)), min(low(a), high(b))); }
static inline v8ui max(v8ui a, v8ui b) { return uint2x4(max(low(a), high(b)), max(low(a), high(b))); }
#endif

typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static v8sf unused _0f = float8(0);
static v8sf unused _1f = float8(1);

static inline v8sf load(const float* a, int index) { return *(v8sf*)(a+index); }
static inline v8sf load(ref<float> a, int index) { return load(a.data, index); }
static inline v8sf loadu(const float* a, int index) {
 struct v8sfu { v8sf v; } __attribute((__packed__, may_alias));
 return ((v8sfu*)(a+index))->v;
}
static inline v8sf loadu(ref<float> a, int index) { return loadu(a.data, index); }

static inline void store(float* const a, int index, v8sf v) { *(v8sf*)(a+index) = v; }
static inline void store(mref<float> a, int index, v8sf v) { store(a.begin(), index, v); }
static inline void storeu(float* const a, int index, v8sf v) { __builtin_ia32_storeups256(a+index, v); }
static inline void storeu(mref<float> a, int index, v8sf v) { storeu(a.begin(), index, v); }

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
static inline v8ui lessThan(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_LT_OS); }
static inline v8ui greaterThan(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_GT_OS); }
static inline v8ui greaterThanOrEqual(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_GTE_OS); }
static inline v8ui notEqual(v8ui a, v8ui b) { return (v8ui)(v8sf)_mm256_cmp_ps((v8sf)a, (v8sf)b, _CMP_NEQ_UQ); }
static inline v8ui equal(v8sf a, v8sf b) { return (v8ui)(v8sf)_mm256_cmp_ps(a, b, _CMP_EQ_OQ); }
#else
static inline v8ui lessThan(v8sf a, v8sf b) { return a < b; }
static inline v8ui greaterThan(v8sf a, v8sf b) { return a > b; }
static inline v8ui greaterThanOrEqual(v8sf a, v8sf b) { return a >= b; }
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

#if __INTEL_COMPILER
static inline void maskStore(float* p, v8ui k, v8sf a) { _mm256_maskstore_ps(p, k, a); }
static inline uint moveMask(v8ui k) { return _mm256_movemask_ps(k); }
static inline uint populationCount(v8ui k) { return _mm_popcnt_u32(k); }
#else
static inline void maskStore(float* p, v8ui k, v8sf a) { __builtin_ia32_maskstoreps256((v8sf*)p, k, a); }
static inline uint moveMask(v8ui k) { return __builtin_ia32_movmskps256(k); }
static inline uint populationCount(v8ui k) { return __builtin_popcount(moveMask(k)); }
#endif
static inline void compressStore(uint* p, v8ui k, v8ui a) {\
 uint mask = moveMask(k);
 for(uint i=0; i<8; i++) if(mask&(1<<i)) { *p = extract(a, i); p++; }
}

typedef float v4sf __attribute((__vector_size__ (16)));
#if __INTEL_COMPILER
#define reduce(op) \
static inline float op(v8sf x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = _mm_##op##_ps(_mm256_extractf128_ps(x, 1), _mm256_castps256_ps128(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = _mm_##op##_ps(x128, _mm_movehl_ps(x128, x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = _mm_##op##_ss(x64, _mm_shuffle_ps(x64, x64, 0x55)); \
    return _mm_cvtss_f32(x32); \
}
#elif __clang__
#define reduce(op) \
static inline float op(v8sf x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = __builtin_ia32_##op##ps(__builtin_ia32_vextractf128_ps256(x, 1), __builtin_shufflevector(x, x, 0, 1, 2, 3)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = __builtin_ia32_##op##ps(x128, __builtin_shufflevector(x128, x128, 6, 7, 2, 3)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = __builtin_ia32_##op##ps(x64, __builtin_shufflevector(x64, x64, 1,1,1,1)); \
    return x32[0]; \
}
#else
#define reduce(op) \
static inline float op(v8sf x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = __builtin_ia32_##op##ps(__builtin_ia32_vextractf128_ps256(x, 1), __builtin_ia32_ps_ps256(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = __builtin_ia32_##op##ps(x128, __builtin_ia32_movhlps(x128, x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = __builtin_ia32_##op##ps(x64, __builtin_ia32_shufps(x64, x64, 0x55)); \
    return x32[0]; \
}
#endif
reduce(min)
reduce(max)
#undef reduce

#if __INTEL_COMPILER
#define reduce(op) \
static inline uint op(v8ui x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = _mm_##op##_epi32(high(x),  low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = _mm_##op##_epi32(x128, _mm_movehl_epi32(x128, x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = _mm_##op##_ss(x64, _mm_shuffle_epi32(x64, x64, 0x55)); \
    return _mm_cvtss_f32(x32); \
}
#elif __clang__
#define reduce(op) \
static inline uint op(v8ui x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = __builtin_ia32_p##op##sd128(high(x), low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = __builtin_ia32_p##op##sd128(x128, __builtin_shufflevector(x128, x128, 6, 7, 2, 3)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = __builtin_ia32_p##op##sd128(x64, __builtin_shufflevector(x64, x64, 1,1,1,1)); \
    return x32[0]; \
}
#else
#define reduce(op) \
static inline uint op(v8ui x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4sf x128 = __builtin_ia32_p##op##sd128(high(x), low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4sf x64 = __builtin_ia32_p##op##sd128(x128, __builtin_ia32_movhlepi32(x128, x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4sf x32 = __builtin_ia32_p##op##sd128(x64, __builtin_ia32_shufepi32(x64, x64, 0x55)); \
    return x32[0]; \
}
#endif
reduce(min)
reduce(max)
#undef reduce

static constexpr int simd = 8; // SIMD size
typedef v8sf vXsf;
typedef v8ui vXui;
typedef v8ui maskX;
inline vXsf floatX(float x) { return float8(x); }

#endif
