#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#include <immintrin.h>
typedef float v4sf __attribute((__vector_size__ (16)));
typedef float v8sf __attribute((__vector_size__ (32)));
typedef short v8hf __attribute((__vector_size__ (16)));
typedef short v16hf __attribute((__vector_size__ (32)));

typedef float v8sf __attribute((vector_size(32),may_alias));
inline v8sf float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }

typedef int v8si __attribute((vector_size(32),may_alias));
inline v8si intX(int x) { return (v8si){x,x,x,x,x,x,x,x}; }
static v8si unused _0i = intX(0);
static v8si unused _1i = intX(-1);

static inline v8sf gather(const float* P, v8si i) { return __builtin_ia32_gatherd_ps256(_0i, P, i, _1i, sizeof(float)); }

struct v16si;

/// 16-wide vector operations using 2 v8sf AVX registers
struct v16sf {
    v8sf r1,r2;
    v16sf(){}
    v16sf(float x){ r1 = r2 = float8(x);}
    v16sf(const v8sf& r1, const v8sf& r2):r1(r1),r2(r2){}
    v16sf(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10, float x11, float x12, float x13, float x14, float x15):r1(__extension__ (__m256){x7,x6,x5,x4,x3,x2,x1,x0}),r2(__extension__ (__m256){x15,x14,x13,x12,x11,x10,x9,x8}){}
    float& operator [](uint i) { return ((float*)this)[i]; }
    const float& operator [](uint i) const { return ((float*)this)[i]; }
    operator v16si() const;
};

inline v16sf& operator+=(v16sf& a, v16sf b) { a.r1 += b.r1; a.r2 += b.r2; return a; }
inline v16sf operator+(v16sf a, v16sf b) { return v16sf(a.r1 + b.r1, a.r2 + b.r2); }
//inline v16sf operator+(float a, v16sf b) { return v16sf(float8(a) + b.r1, float8(a) + b.r2); }
//inline v16sf operator+(v16sf b, float a) { return a+b; }
//inline v16sf operator*(float a, v16sf b) { return v16sf(float8(a) * b.r1, float8(a) * b.r2); }
inline v16sf operator*(v16sf a, v16sf b) { return v16sf(a.r1 * b.r1, a.r2 * b.r2); }
inline v16sf operator /(const int one unused, v16sf d) { assert(one==1); return v16sf(_mm256_rcp_ps(d.r1),_mm256_rcp_ps(d.r2)); }

inline float sum8(v8sf x) {
    const v4sf sumQuad = __builtin_shufflevector(x, x, 0, 1, 2, 3) + __builtin_shufflevector(x, x, 4, 5, 6, 7); // 0 + 4, 1 + 5, 2 + 6, 3 + 7
    const v4sf sumDual = sumQuad + __builtin_shufflevector(sumQuad, sumQuad, 2, 3, -1, -1); // 0+4 + 2+6, 1+5 + 3+7 (+movehl)
    return (sumDual + __builtin_shufflevector(sumDual, sumDual, 1, -1, -1, -1))[0]; // 0+4+2+6 + 1+5+3+7
}
inline float sum16(v16sf v) { return sum8(v.r1+v.r2); }

//inline string str(const v16sf v) { return "v16sf("_+str(ref<float>((float*)&v,16))+")"_; }

/// 16-wide vector operations using 2 v8si AVX registers
struct v16si {
    v8si r1,r2;
    v16si(){}
    explicit v16si(int x){ r1 = r2 = intX(x); }
    v16si(const v8si r1, const v8si r2):r1(r1),r2(r2){}
    v16si(int x0, int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, int x9, int x10, int x11, int x12, int x13, int x14, int x15):r1((v8si){x7,x6,x5,x4,x3,x2,x1,x0}),r2((v8si){x15,x14,x13,x12,x11,x10,x9,x8}){}
    int& operator [](uint i) { return ((int*)this)[i]; }
    const int& operator [](uint i) const { return ((int*)this)[i]; }
    operator v16sf() const;
};

inline v16sf::operator v16si() const { return {(v8si)r1, (v8si)r2}; }
inline v16si::operator v16sf() const { return {(v8sf)r1, (v8sf)r2}; }

inline v16si operator*(int a, v16si b) { return v16si(intX(a) * b.r1, intX(a) * b.r2); }

static inline v16sf gather(const float* P, v16si i) { return v16sf(gather(P, i.r1), gather(P, i.r2)); }

inline v16si operator<(float a, v16sf b) { return v16si(float8(a) < b.r1, float8(a) < b.r2); }
inline v16si operator<=(float a, v16sf b) { return v16si(float8(a) <= b.r1, float8(a) <= b.r2); }
inline v16si operator>=(float a, v16sf b) { return v16si(float8(a) >= b.r1, float8(a) >= b.r2); }
inline v16si operator>(float a, v16sf b) { return v16si(float8(a) > b.r1, float8(a) > b.r2); }

inline v16si operator<(v16sf b, float a) { return a > b; }
inline v16si operator<=(v16sf b, float a) { return a >= b; }
inline v16si operator>=(v16sf b, float a) { return a <= b; }
inline v16si operator>(v16sf b, float a) { return a < b; }

inline v16si operator<(v16sf a, v16sf b) { return v16si(a.r1 < b.r1, a.r2 < b.r2); }
inline v16si operator<=(v16sf a, v16sf b) { return v16si(a.r1 <= b.r1, a.r2 <= b.r2); }
inline v16si operator>=(v16sf a, v16sf b) { return v16si(a.r1 >= b.r1, a.r2 >= b.r2); }
inline v16si operator>(v16sf a, v16sf b) { return v16si(a.r1 > b.r1, a.r2 > b.r2); }

inline uint mask(v16si m) { return __builtin_ia32_movmskps256(m.r1)|(__builtin_ia32_movmskps256(m.r2)<<8); }

typedef uint8 mask8;
static const unused v8si selectMask {1<<7, 1<<6, 1<<5, 1<<4,  1<<3, 1<<2, 1<<1, 1<<0};
inline v8si mask(const uint8 mask) { return (intX(mask) & selectMask) != _0i; }
typedef uint16 mask16;
inline v16si mask(const mask16 m) { return v16si(mask(mask8(m)), mask(mask8(m>>8))); }

inline v16si operator &(v16si a, v16si b) { return v16si(a.r1 & b.r1, a.r2 & b.r2); }
inline v16sf operator &(v16sf a, v16si b) { return (v16sf)((v16si)a & b); }

inline v16sf blend(v16sf A, v16sf B, v16si mask) { return v16sf(__builtin_ia32_blendvps256(A.r1, B.r1, mask.r1),__builtin_ia32_blendvps256(A.r2, B.r2, mask.r2)); }

inline void maskstore(v16sf& P, v16si M, v16sf A) {
    __builtin_ia32_maskstoreps256(&P.r1, M.r1, A.r1);
    __builtin_ia32_maskstoreps256(&P.r2, M.r2, A.r2);
}

inline v16si cvtt(const v16sf v) { return {__builtin_ia32_cvttps2dq256(v.r1), __builtin_ia32_cvttps2dq256(v.r2)}; }

/// 16-wide vector operations using 2 v8si AVX registers
inline v16hf toHalf(const v16sf v) {
    v4sf a = __builtin_ia32_vcvtps2ph256(v.r1, 0);
    v4sf b = __builtin_ia32_vcvtps2ph256(v.r2, 0);
    return __builtin_shufflevector(a, b, 0, 1, 2, 3, 4, 5, 6, 7);
}

/// 16-wide vector operations using 2 v8si AVX registers
inline v16sf toFloat(const v16hf v) {
    return v16sf(__builtin_ia32_vcvtph2ps256(__builtin_shufflevector(v, v, 0+0, 0+1, 0+2, 0+3, 0+4, 0+5, 0+6, 0+7)),
                 __builtin_ia32_vcvtph2ps256(__builtin_shufflevector(v, v, 8+0, 8+1, 8+2, 8+3, 8+4, 8+5, 8+6, 8+7)));
}

