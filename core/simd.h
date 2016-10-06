#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#include <immintrin.h>
typedef float v4sf __attribute((__vector_size__ (16)));
typedef float v8sf __attribute((__vector_size__ (32)));

#if 0
typedef float v16sf __attribute((__vector_size__ (64)));

inline v16sf float16(float f) { return (v16sf){f,f,f,f,f,f,f,f,f,f,f,f,f,f,f,f}; }
static v16sf unused _0f = float16(0);
static v16sf unused _1f = float16(1);

typedef int v16si __attribute((__vector_size__ (64)));

typedef uint16 mask16;

inline mask16 mask(v16sf A) { return __builtin_ia32_movmskps256(__builtin_shufflevector(A, A, 0,1,2,3,4,5,6,7))|
                                  (__builtin_ia32_movmskps256(__builtin_shufflevector(A, A, 8,9,10,11,12,13,14,15))<<8); }

inline v16sf and(v16sf A, v16si M) { return (v16sf)((v16si)A&M); }

static inline v16sf blend(mask16 k, v16sf a, v16sf b) { return _mm512_mask_blend_ps(k, a, b); }

inline void store(v16sf& P, v16sf A, mask16 M) { __builtin_ia32_storeaps512_mask(&P, A, M); }

inline float sum(v16sf A) {
    v16sf t = A + _mm512_shuffle_f32x4(A,A,_MM_SHUFFLE(0,0,3,2));
    v4sf r = _mm512_castps512_ps128(_mm512_add_ps(t,_mm512_shuffle_f32x4(t,t,_MM_SHUFFLE(0,0,0,1))));
    r = _mm_hadd_ps(r,r);
    return _mm_cvtss_f32(_mm_hadd_ps(r,r));
}
#else
#pragma once
#include "string.h"

// AVX intrinsics
#define __AVX__ 1
#include <immintrin.h>
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include <avxintrin.h>
#endif
typedef int v8si __attribute((vector_size(32),may_alias));
typedef float v8sf __attribute((vector_size(32),may_alias));

/// 16-wide vector operations using 2 v8sf AVX registers
struct v16sf {
    v8sf r1,r2;
    v16sf(){}
    v16sf(float x){r1=r2= _mm256_set1_ps(x);}
    v16sf(const v8sf& r1, const v8sf& r2):r1(r1),r2(r2){}
    v16sf(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10, float x11, float x12, float x13, float x14, float x15):r1(__extension__ (__m256){x7,x6,x5,x4,x3,x2,x1,x0}),r2(__extension__ (__m256){x15,x14,x13,x12,x11,x10,x9,x8}){}
    float& operator [](uint i) { return ((float*)this)[i]; }
    const float& operator [](uint i) const { return ((float*)this)[i]; }
};

inline v16sf operator +(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_add_ps(A,b.r1),_mm256_add_ps(A,b.r2));
}
inline v16sf operator +(v16sf b, float a) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_add_ps(A,b.r1),_mm256_add_ps(A,b.r2));
}
inline v16sf operator +(v16sf a, v16sf b) {
    return v16sf(_mm256_add_ps(a.r1,b.r1),_mm256_add_ps(a.r2,b.r2));
}

inline v16sf operator *(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_mul_ps(A,b.r1),_mm256_mul_ps(A,b.r2));
}
inline v16sf operator *(v16sf a, v16sf b) {
    return v16sf(_mm256_mul_ps(a.r1,b.r1),_mm256_mul_ps(a.r2,b.r2));
}
inline v16sf operator /(const int one unused, v16sf d) {
    assert(one==1);
    return v16sf(_mm256_rcp_ps(d.r1),_mm256_rcp_ps(d.r2));
}

inline v16sf operator |(v16sf a, v16sf b) {
    return v16sf(_mm256_or_ps(a.r1,b.r1),_mm256_or_ps(a.r2,b.r2));
}
inline v16sf operator &(v16sf a, v16sf b) {
    return v16sf(_mm256_and_ps(a.r1,b.r1),_mm256_and_ps(a.r2,b.r2));
}

inline v16sf operator <(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(A, b.r1, _CMP_LT_OQ),_mm256_cmp_ps(A, b.r2, _CMP_LT_OQ));
}
inline v16sf operator <=(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(A, b.r1, _CMP_LE_OQ),_mm256_cmp_ps(A, b.r2, _CMP_LE_OQ));
}
inline v16sf operator >=(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(A, b.r1, _CMP_GE_OQ),_mm256_cmp_ps(A, b.r2, _CMP_GE_OQ));
}
inline v16sf operator >(float a, v16sf b) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(A, b.r1, _CMP_GT_OQ),_mm256_cmp_ps(A, b.r2, _CMP_GT_OQ));
}

inline v16sf operator <(v16sf b, float a) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(b.r1, A, _CMP_LT_OQ),_mm256_cmp_ps(b.r2, A, _CMP_LT_OQ));
}
inline v16sf operator <=(v16sf b, float a) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(b.r1, A, _CMP_LE_OQ),_mm256_cmp_ps(b.r2, A, _CMP_LE_OQ));
}
inline v16sf operator >=(v16sf b, float a) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(b.r1, A, _CMP_GE_OQ),_mm256_cmp_ps(b.r2, A, _CMP_GE_OQ));
}
inline v16sf operator >(v16sf b, float a) {
    v8sf A=_mm256_set1_ps(a);
    return v16sf(_mm256_cmp_ps(b.r1, A, _CMP_GT_OQ),_mm256_cmp_ps(b.r2, A, _CMP_GT_OQ));
}

inline v16sf operator <(v16sf a, v16sf b) {
    return v16sf(_mm256_cmp_ps(a.r1, b.r1, _CMP_LT_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_LT_OQ));
}
inline v16sf operator <=(v16sf a, v16sf b) {
    return v16sf(_mm256_cmp_ps(a.r1, b.r1, _CMP_LE_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_LE_OQ));
}
inline v16sf operator >=(v16sf a, v16sf b) {
    return v16sf(_mm256_cmp_ps(a.r1, b.r1, _CMP_GE_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_GE_OQ));
}
inline v16sf operator >(v16sf a, v16sf b) {
    return v16sf(_mm256_cmp_ps(a.r1, b.r1, _CMP_GT_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_GT_OQ));
}
inline uint mask(v16sf m) { return _mm256_movemask_ps(m.r1)|(_mm256_movemask_ps(m.r2)<<8); }

inline float sum8(v8sf x) {
    // hiQuad = ( x7, x6, x5, x4 )
    const __m128 hiQuad = _mm256_extractf128_ps(x, 1);
    // loQuad = ( x3, x2, x1, x0 )
    const __m128 loQuad = _mm256_castps256_ps128(x);
    // sumQuad = ( x3 + x7, x2 + x6, x1 + x5, x0 + x4 )
    const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
    // loDual = ( -, -, x1 + x5, x0 + x4 )
    const __m128 loDual = sumQuad;
    // hiDual = ( -, -, x3 + x7, x2 + x6 )
    const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
    // sumDual = ( -, -, x1 + x3 + x5 + x7, x0 + x2 + x4 + x6 )
    const __m128 sumDual = _mm_add_ps(loDual, hiDual);
    // lo = ( -, -, -, x0 + x2 + x4 + x6 )
    const __m128 lo = sumDual;
    // hi = ( -, -, -, x1 + x3 + x5 + x7 )
    const __m128 hi = _mm_shuffle_ps(sumDual, sumDual, 0x1);
    // sum = ( -, -, -, x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7 )
    const __m128 sum = _mm_add_ss(lo, hi);
    return _mm_cvtss_f32(sum);
}
inline float sum16(v16sf v) { return sum8(v.r1+v.r2); }

inline v16sf blend16(float a, v16sf b, v16sf mask) {
    v8sf A = _mm256_set1_ps(a);
    return v16sf(_mm256_blendv_ps(A,b.r1,mask.r1),_mm256_blendv_ps(A,b.r2,mask.r2));
}
inline v16sf blend16(float a, float b, v16sf mask) {
    v8sf A = _mm256_set1_ps(a);
    v8sf B = _mm256_set1_ps(b);
    return v16sf(_mm256_blendv_ps(A,B,mask.r1),_mm256_blendv_ps(A,B,mask.r2));
}

inline void maskstore(v16sf& P, v16sf M, v16sf A) {
    _mm256_maskstore_ps((float*)&P.r1,(__m256i)M.r1,A.r1);
    _mm256_maskstore_ps((float*)&P.r2,(__m256i)M.r2,A.r2);
}

inline string str(const v16sf v) { return "v16sf("_+str(ref<float>((float*)&v,16))+")"_; }

/// 16-wide vector operations using 2 v8si AVX registers
struct v16si {
    v8si r1,r2;
    v16si(){}
    v16si(int x){r1=r2= _mm256_set1_epi32(x);}
    v16si(const v8si& r1, const v8si& r2):r1(r1),r2(r2){}
    int& operator [](uint i) { return ((int*)this)[i]; }
    const int& operator [](uint i) const { return ((int*)this)[i]; }
};

inline v16si cvtt(const v16sf v) { return {__builtin_ia32_cvttps2dq256(v.r1), __builtin_ia32_cvttps2dq256(v.r2)}; }

#endif
