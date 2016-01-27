#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#if __INTEL_COMPILER && __MIC__
typedef int v16si __attribute((__vector_size__ (64)));
inline v16si intX(int x) { return (v16si){x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x}; }
static v16si unused _0i = intX(0);
//static v16si unused _1i = intX(-1);
static v16si unused _seqi {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

#include <immintrin.h>

static inline v16si load(const int* a, int index) { return *(v16si*)(a+index); }
static inline v16si gather(const int* P, v16si i) { return _mm512_i32gather_epi32(i, (const int*)P, sizeof(int)); }

static inline v16si min(v16si a, v16si b) { return _mm512_min_epi32(a, b); }
static inline v16si max(v16si a, v16si b) { return _mm512_max_epi32(a, b); }
static inline int min(v16si x) { return _mm512_reduce_min_epi32(x); }
static inline int max(v16si x) { return _mm512_reduce_max_epi32(x); }

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
static inline v16sf rsqrt(v16sf x) { return _mm512_rsqrt23_ps(x); }

static inline int extract(v16si x, int i) { union { int e[16]; v16si v; } X; X.v = x; return X.e[i]; }
static inline void insert(v16si& x, int i, int v) { union { int e[16]; v16si v; } X; X.v = x; X.e[i] = v; x=X.v; }
static inline float extract(v16sf x, int i) { union { float e[16]; v16sf v; } X; X.v = x; return X.e[i]; }

static inline v16sf gather(const float* P, v16si i) { return _mm512_i32gather_ps(i, P, sizeof(float)); }

static inline void scatter(int* const P, const v16si i, const v16si x) { _mm512_i32scatter_epi32(P, i, x, sizeof(float)); }
static inline void scatter(float* const P, const v16si i, const v16sf x) { _mm512_i32scatter_ps(P, i, x, sizeof(float)); }

static inline uint16 lessThan(v16si a, v16si b) { return _mm512_cmp_epi32_mask(a, b, _CMP_LT_OS); }
static inline uint16 lessThan(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_LT_OS); }
static inline uint16 greaterThan(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_GT_OS); }
static inline uint16 greaterThanOrEqual(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_GE_OS); }
static inline uint16 equal(v16sf a, v16sf b) { return _mm512_cmp_ps_mask(a, b, _CMP_EQ_OQ); }
static inline uint16 notEqual(v16si a, v16si b) { return _mm512_cmp_epi32_mask(a, b, _CMP_NEQ_UQ); }

static inline v16sf blend(uint16 k, v16sf a, v16sf b) { return _mm512_mask_blend_ps(k, a, b); }

static inline float min(v16sf x) { return _mm512_reduce_min_ps(x); }
static inline float max(v16sf x) { return _mm512_reduce_max_ps(x); }

static inline v16si convert(v16sf x) {
 return _mm512_cvtfxpnt_round_adjustps_epi32(x, _MM_FROUND_TO_NEG_INF, _MM_EXPADJ_NONE);
}

static inline void maskStore(float* p, uint16 k, v16sf a) { _mm512_mask_store_ps(p, k, a); }
static inline uint countBits(uint16 k) { return _mm_countbits_32(k); }
#if KNIGHTS_LANDING
static inline void compressStore(int* p, uint16 k, v16si a) { _mm512_mask_compressstoreu_epi32(p, k, a); }
#else
static inline void compressStore(int* p, uint16 mask, v16si a) {
  for(uint i=0; i<16; i++) if(mask&(1<<i)) { *p = extract(a, i); p++; }
}
#endif

static constexpr int simd = 16; // SIMD size
typedef v16sf vXsf;
typedef v16si vXsi;
typedef uint16 maskX;
inline vXsf floatX(float x) { return float16(x); }

#else

typedef int v8si __attribute((__vector_size__ (32)));
inline v8si intX(int x) { return (v8si){x,x,x,x,x,x,x,x}; }
static v8si unused _0i = intX(0);
static v8si unused _1i = intX(-1);
static v8si unused _seqi = (v8si){0,1,2,3,4,5,6,7};

static inline v8si load(const int* a, int index) { return *(v8si*)(a+index); }

#if __INTEL_COMPILER
#include <immintrin.h>
#endif

typedef int v4si __attribute((__vector_size__ (16)));
#if __INTEL_COMPILER
static inline int extract(v4si x, int i) { return _mm_extract_epi32(x, i); }
static inline int extract(v8si x, int i) { union { int e[8]; v8si v; } X; X.v = x; return X.e[i]; }
//static inline float extract(v8si x, int i) { return _mm256_extract_epi32(x, i); }
static inline void insert(v8si& x, int i, int v) { union { int e[8]; v8si v; } X; X.v = x; X.e[i] = v; x=X.v; }
#else
static inline int extract(v8si x, int i) { return x[i]; }
static inline void insert(v8si& x, int i, int v) { x[i] = v; }
#endif

static inline v8si gather(const int* P, v8si i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_d256(_0i, (const v8si*)P, i, _1i, sizeof(int));
#elif __INTEL_COMPILER
 return _mm256_i32gather_epi32((const int*)P, (v8si)i, sizeof(int));
#else
 return (v8si)__builtin_ia32_gathersiv8si((v8si)_0i, (const int*)P, (v8si)i, (v8si)_1i, sizeof(int));
#endif
#elif __INTEL_COMPILER
 union { int e[8]; v8si v; } I; I.v = i;
 return (v8si){P[I.e[0]], P[I.e[1]], P[I.e[2]], P[I.e[3]], P[I.e[4]], P[I.e[5]], P[I.e[6]], P[I.e[7]]};
#else
 return (v8si){P[i[0]], P[i[1]], P[i[2]], P[i[3]], P[i[4]], P[i[5]], P[i[6]], P[i[7]]};
#endif
}

static inline void scatter(int* const P, const v8si i, const v8si x) {
#if __INTEL_COMPILER
 union { int e[8]; v8si v; } I; I.v = i;
 union { int e[8]; v8si v; } X; X.v = x;
 P[I.e[0]] = X.e[0]; P[I.e[1]] = X.e[1]; P[I.e[2]] = X.e[2]; P[I.e[3]] = X.e[3];
 P[I.e[4]] = X.e[4]; P[I.e[5]] = X.e[5]; P[I.e[6]] = X.e[6]; P[I.e[7]] = X.e[7];
#else
 P[i[0]] = x[0]; P[i[1]] = x[1]; P[i[2]] = x[2]; P[i[3]] = x[3];
 P[i[4]] = x[4]; P[i[5]] = x[5]; P[i[6]] = x[6]; P[i[7]] = x[7];
#endif
}

typedef long long m128i __attribute__((__vector_size__(16)));
typedef long long m256i __attribute__((__vector_size__(32)));
#if AVX2
static inline v8si min(v8si a, v8si b) { return __builtin_ia32_pminud256(a, b); }
static inline v8si max(v8si a, v8si b) { return __builtin_ia32_pmaxud256(a, b); }
#else
#if __INTEL_COMPILER
static inline v4si low(v8si x) { return _mm256_castsi256_si128(x); }
static inline v4si high(v8si x) { return _mm256_extractf128_si256(x, 1); }
static inline v8si int2x4(v4si a, v4si b) {
 return _mm256_insertf128_si256(_mm256_castsi128_si256((m128i)a), (m128i)b, 1);
}
#elif __clang__
static inline v4si low(v8si x) { return __builtin_shufflevector(x, x, 0, 1, 2, 3); }
static inline v4si high(v8si x) { return __builtin_ia32_vextractf128_si256(x, 1); }
static inline v8si int2x4(v4si a, v4si b) {
 return __builtin_ia32_vinsertf128_si256((m256i)__builtin_shufflevector((m128i)a ,(m128i)a, 0, 1, -1, -1),
                                                                     b, 1);
}
#else
static inline v4si low(v8si x) { return __builtin_ia32_si_si256(x); }
static inline v4si high(v8si x) { return __builtin_ia32_vextractf128_si256(x, 1); }
static inline v8si int2x4(v4si a, v4si b) {
 return __builtin_ia32_vinsertf128_si256(__builtin_ia32_si256_si(a), b, 1); }
#endif

static inline v4si min(v4si a, v4si b) { return __builtin_ia32_pminsd128(a, b); }
static inline v4si max(v4si a, v4si b) { return __builtin_ia32_pmaxsd128(a, b); }
static inline v8si min(v8si a, v8si b) { return int2x4(min(low(a), low(b)), min(high(a), high(b))); }
static inline v8si max(v8si a, v8si b) { return int2x4(max(low(a), low(b)), max(high(a), high(b))); }
#endif

#if __INTEL_COMPILER
static inline v4si lessThan(v4si a, v4si b) { return _mm_cmplt_epi32(a, b); }
static inline v8si lessThan(v8si a, v8si b) { return int2x4(lessThan(low(a), low(b)), lessThan(high(a), high(b))); }
#else
static inline v8si lessThan(v8si a, v8si b) { return a < b; }
#endif

typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static v8sf unused _0f = float8(0);
static v8sf unused _1f = float8(1);

static inline v8sf load(const float* a, int index) { return *(v8sf*)(a+index); }
static inline v8sf load(ref<float> a, int index) { return load(a.data, index); }
#if __clang__
static inline v8sf loadu(const float* a, int index) {
#undef packed
 struct u { v8sf v; } __attribute((packed, may_alias));
 return ((u*)(a+index))->v;
}
#elif __INTEL_COMPILER
static inline v8sf loadu(const float* a, int index) { return _mm256_loadu_ps(a+index); }
#elif __GNUC__
static inline v8sf loadu(const float* a, int index) { return __builtin_ia32_loadups256(a+index); }
#endif
static inline v8sf loadu(ref<float> a, int index) { return loadu(a.data, index); }

static inline void store(float* const a, int index, v8sf v) { *(v8sf*)(a+index) = v; }
static inline void store(mref<float> a, int index, v8sf v) { store(a.begin(), index, v); }
static inline void storeu(float* const a, int index, v8sf v) { __builtin_ia32_storeups256(a+index, v); }
static inline void storeu(mref<float> a, int index, v8sf v) { storeu(a.begin(), index, v); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }
static inline v8sf rsqrt(v8sf x) { return __builtin_ia32_rsqrtps256(x); }
static inline v8sf rcp(v8sf x) { return __builtin_ia32_rcpps256(x); }

#if __INTEL_COMPILER
static inline float extract(v8sf x, int i) { union { float e[8]; v8sf v; } X; X.v = x; return X.e[i]; }
#else
static inline float extract(v8sf x, int i) { return x[i]; }
#endif
static inline v8sf gather(const float* P, v8si i) {
#if __AVX2__
#if __clang__
 return __builtin_ia32_gatherd_ps256(_0f, (const v8sf*)P, i, _1i, sizeof(float));
#elif __INTEL_COMPILER
 return _mm256_i32gather_ps(P, (v8si)i, sizeof(float));
#else
 return __builtin_ia32_gathersiv8sf(_0f, P, (v8si)i, (v8sf)_1i, sizeof(float));
#endif
#elif __INTEL_COMPILER
 union { int e[8]; v8si v; } I; I.v = i;
 return (v8sf){P[I.e[0]], P[I.e[1]], P[I.e[2]], P[I.e[3]], P[I.e[4]], P[I.e[5]], P[I.e[6]], P[I.e[7]]};
#else
 return (v8sf){P[i[0]], P[i[1]], P[i[2]], P[i[3]], P[i[4]], P[i[5]], P[i[6]], P[i[7]]};
#endif
}
static inline v8sf gather(ref<float> P, v8si i) { return gather(P.data, i); }

static inline void scatter(float* const P, const v8si i, const v8sf x) {
#if __INTEL_COMPILER
 union { int e[8]; v8si v; } I; I.v = i;
 union { float e[8]; v8sf v; } X; X.v = x;
 P[I.e[0]] = X.e[0]; P[I.e[1]] = X.e[1]; P[I.e[2]] = X.e[2]; P[I.e[3]] = X.e[3];
 P[I.e[4]] = X.e[4]; P[I.e[5]] = X.e[5]; P[I.e[6]] = X.e[6]; P[I.e[7]] = X.e[7];
#else
 P[i[0]] = x[0]; P[i[1]] = x[1]; P[i[2]] = x[2]; P[i[3]] = x[3];
 P[i[4]] = x[4]; P[i[5]] = x[5]; P[i[6]] = x[6]; P[i[7]] = x[7];
#endif
}

#if __INTEL_COMPILER
static inline v8si lessThan(v8sf a, v8sf b) { return (v8si)(v8sf)_mm256_cmp_ps(a, b, _CMP_LT_OS); }
static inline v8si greaterThan(v8sf a, v8sf b) { return (v8si)(v8sf)_mm256_cmp_ps(a, b, _CMP_GT_OS); }
static inline v8si greaterThanOrEqual(v8sf a, v8sf b) { return (v8si)(v8sf)_mm256_cmp_ps(a, b, _CMP_GE_OS); }
static inline v8si equal(v8sf a, v8sf b) { return (v8si)(v8sf)_mm256_cmp_ps(a, b, _CMP_EQ_OQ); }
static inline v8si notEqual(v8si a, v8si b) {
#if AVX2
 return (__m256i)_mm256_andnot_ps((__m256)(__m256i)_mm256_cmpeq_epi32(a, b), (__m256)(__m256i)_1i);
#else
 return (v8si)(v8sf)_mm256_cmp_ps((v8sf)a, (v8sf)b, _CMP_NEQ_UQ);
#endif
}
#else
static inline v8si lessThan(v8sf a, v8sf b) { return a < b; }
static inline v8si greaterThan(v8sf a, v8sf b) { return a > b; }
static inline v8si greaterThanOrEqual(v8sf a, v8sf b) { return a >= b; }
static inline v8si notEqual(v8si a, v8si b) { return a != b; }
static inline v8si equal(v8sf a, v8sf b) { return a == b; }
#endif

static const unused v8si selectMask {1<<7, 1<<6, 1<<5, 1<<4,  1<<3, 1<<2, 1<<1, 1<<0};
static inline v8si expandMask(const uint8 mask) { return notEqual(intX(mask) & selectMask, _0i); }

#if __clang__
static inline v8sf blend(v8si k, v8sf a, v8sf b) { return __builtin_ia32_blendvps256(a, b, k); }
#else
static inline v8sf blend(v8si k, v8sf a, v8sf b) { return __builtin_ia32_blendvps256(a, b, (v8sf)k); }
#endif
static inline v8sf blend(uint8 k, v8sf a, v8sf b) { return blend(expandMask(k), a, b); }

static inline v8sf mask(v8si a, v8sf b) { return (v8sf)(a & (v8si)b); }
static inline v8sf maskSub(v8sf a, v8si k, v8sf b) { return a - mask(k, b); }
static inline v8sf mask3_fmadd(v8sf a, v8sf b, v8sf c, v8si k) { return mask(k, a * b) + c; }

#if __INTEL_COMPILER
static inline v8si convert(v8sf x) { return _mm256_cvttps_epi32(x); }
#else
static inline v8si convert(v8sf x) { return __builtin_ia32_cvttps2dq256(x); }
#endif

#if __INTEL_COMPILER
static inline void maskStore(float* p, v8si k, v8sf a) { _mm256_maskstore_ps(p, k, a); }
static inline int moveMask(v8si k) { return _mm256_movemask_ps((v8sf)k); }
static inline uint countBits(v8si k) { return _mm_countbits_32(moveMask(k)); }
#else
static inline void maskStore(float* p, v8si k, v8sf a) { __builtin_ia32_maskstoreps256((v8sf*)p, k, a); }
static inline uint moveMask(v8si k) { return __builtin_ia32_movmskps256((v8sf)k); }
static inline uint countBits(v8si k) { return __builtin_popcount(moveMask(k)); }
#endif
static inline void compressStore(int* p, v8si k, v8si a) {\
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
static inline int op(v8si x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4si x128 = _mm_##op##_epi32(high(x),  low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4si x64 = _mm_##op##_epi32(x128, (__m128i)_mm_movehl_ps((v4sf)x128, (v4sf)x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4si x32 = _mm_##op##_epi32(x64, _mm_shuffle_epi32(x64, 0x55)); \
    return extract(x32, 0); \
}
#elif __clang__
#define reduce(op) \
static inline int op(v8si x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4si x128 = __builtin_ia32_p##op##sd128(high(x), low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4si x64 = __builtin_ia32_p##op##sd128(x128, __builtin_shufflevector(x128, x128, 6, 7, 2, 3)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4si x32 = __builtin_ia32_p##op##sd128(x64, __builtin_shufflevector(x64, x64, 1,1,1,1)); \
    return x32[0]; \
}
#else
#define reduce(op) \
static inline int op(v8si x) { \
    /* ( x3+x7, x2+x6, x1+x5, x0+x4 ) */ \
    const v4si x128 = __builtin_ia32_p##op##sd128(high(x), low(x)); \
    /* ( -, -, x1+x3+x5+x7, x0+x2+x4+x6 ) */ \
    const v4si x64 = __builtin_ia32_p##op##sd128(x128, (v4si)__builtin_ia32_movhlps((v4sf)x128, (v4sf)x128)); \
    /* ( -, -, -, x0+x1+x2+x3+x4+x5+x6+x7 ) */ \
    const v4si x32 = __builtin_ia32_p##op##sd128(x64, (v4si)__builtin_ia32_shufps((v4sf)x64, (v4sf)x64, 0x55)); \
    return x32[0]; \
}
#endif
reduce(min)
reduce(max)
#undef reduce

static constexpr int simd = 8; // SIMD size
typedef v8sf vXsf;
typedef v8si vXsi;
typedef v8si maskX;
inline vXsf floatX(float x) { return float8(x); }

#endif
