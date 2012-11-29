#pragma once
#include "string.h"

// AVX intrinsics
#define __AVX__ 1
#include "immintrin.h"
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include "avxintrin.h"
#endif
typedef float float8 __attribute((vector_size(32),may_alias));

/// 16-wide Vector operations using 2 float8 AVX registers
struct vec16 {
    float8 r1,r2;
    vec16(){}
    vec16(float x){r1=r2= _mm256_set1_ps(x);}
    vec16(const float8& r1, const float8& r2):r1(r1),r2(r2){}
    vec16(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10, float x11, float x12, float x13, float x14, float x15):r1(__extension__ (__m256){x7,x6,x5,x4,x3,x2,x1,x0}),r2(__extension__ (__m256){x15,x14,x13,x12,x11,x10,x9,x8}){}
    float& operator [](uint i) { return ((float*)this)[i]; }
    const float& operator [](uint i) const { return ((float*)this)[i]; }
};

inline vec16 operator +(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_add_ps(A,b.r1),_mm256_add_ps(A,b.r2));
}
inline vec16 operator +(vec16 b, float a) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_add_ps(A,b.r1),_mm256_add_ps(A,b.r2));
}
inline vec16 operator +(vec16 a, vec16 b) {
    return vec16(_mm256_add_ps(a.r1,b.r1),_mm256_add_ps(a.r2,b.r2));
}

inline vec16 operator *(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_mul_ps(A,b.r1),_mm256_mul_ps(A,b.r2));
}
inline vec16 operator *(vec16 a, vec16 b) {
    return vec16(_mm256_mul_ps(a.r1,b.r1),_mm256_mul_ps(a.r2,b.r2));
}
inline vec16 operator /(const int one unused, vec16 d) {
    assert(one==1);
    return vec16(_mm256_rcp_ps(d.r1),_mm256_rcp_ps(d.r2));
}

inline vec16 operator |(vec16 a, vec16 b) {
    return vec16(_mm256_or_ps(a.r1,b.r1),_mm256_or_ps(a.r2,b.r2));
}
inline vec16 operator &(vec16 a, vec16 b) {
    return vec16(_mm256_and_ps(a.r1,b.r1),_mm256_and_ps(a.r2,b.r2));
}

inline vec16 operator <(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(A, b.r1, _CMP_LT_OQ),_mm256_cmp_ps(A, b.r2, _CMP_LT_OQ));
}
inline vec16 operator <=(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(A, b.r1, _CMP_LE_OQ),_mm256_cmp_ps(A, b.r2, _CMP_LE_OQ));
}
inline vec16 operator >=(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(A, b.r1, _CMP_GE_OQ),_mm256_cmp_ps(A, b.r2, _CMP_GE_OQ));
}
inline vec16 operator >(float a, vec16 b) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(A, b.r1, _CMP_GT_OQ),_mm256_cmp_ps(A, b.r2, _CMP_GT_OQ));
}

inline vec16 operator <(vec16 b, float a) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(b.r1, A, _CMP_LT_OQ),_mm256_cmp_ps(b.r2, A, _CMP_LT_OQ));
}
inline vec16 operator <=(vec16 b, float a) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(b.r1, A, _CMP_LE_OQ),_mm256_cmp_ps(b.r2, A, _CMP_LE_OQ));
}
inline vec16 operator >=(vec16 b, float a) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(b.r1, A, _CMP_GE_OQ),_mm256_cmp_ps(b.r2, A, _CMP_GE_OQ));
}
inline vec16 operator >(vec16 b, float a) {
    float8 A=_mm256_set1_ps(a);
    return vec16(_mm256_cmp_ps(b.r1, A, _CMP_GT_OQ),_mm256_cmp_ps(b.r2, A, _CMP_GT_OQ));
}

inline vec16 operator <(vec16 a, vec16 b) {
    return vec16(_mm256_cmp_ps(a.r1, b.r1, _CMP_LT_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_LT_OQ));
}
inline vec16 operator <=(vec16 a, vec16 b) {
    return vec16(_mm256_cmp_ps(a.r1, b.r1, _CMP_LE_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_LE_OQ));
}
inline vec16 operator >=(vec16 a, vec16 b) {
    return vec16(_mm256_cmp_ps(a.r1, b.r1, _CMP_GE_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_GE_OQ));
}
inline vec16 operator >(vec16 a, vec16 b) {
    return vec16(_mm256_cmp_ps(a.r1, b.r1, _CMP_GT_OQ),_mm256_cmp_ps(a.r2, b.r2, _CMP_GT_OQ));
}
inline uint mask(vec16 m) { return _mm256_movemask_ps(m.r1)|(_mm256_movemask_ps(m.r2)<<8); }

inline float sum8(float8 x) {
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
inline float sum16(vec16 v) { return sum8(v.r1)+sum8(v.r2); }

inline vec16 blend16(float a, vec16 b, vec16 mask) {
    float8 A = _mm256_set1_ps(a);
    return vec16(_mm256_blendv_ps(A,b.r1,mask.r1),_mm256_blendv_ps(A,b.r2,mask.r2));
}
inline vec16 blend16(float a, float b, vec16 mask) {
    float8 A = _mm256_set1_ps(a);
    float8 B = _mm256_set1_ps(b);
    return vec16(_mm256_blendv_ps(A,B,mask.r1),_mm256_blendv_ps(A,B,mask.r2));
}

inline void maskstore(vec16& P, vec16 M, vec16 A) {
    _mm256_maskstore_ps((float*)&P.r1,(__m256i)M.r1,A.r1);
    _mm256_maskstore_ps((float*)&P.r2,(__m256i)M.r2,A.r2);
}

inline string str(const vec16 v) { return "vec16("_+str(ref<float>((float*)&v,16))+")"_; }
