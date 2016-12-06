#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#include <immintrin.h>

typedef uint8 mask8;
typedef uint16 mask16;

typedef uint64 v4uq __attribute((ext_vector_type(4)));

typedef int v2si __attribute((ext_vector_type(2)));
typedef int v4si __attribute((ext_vector_type(4)));
typedef int v8si __attribute((ext_vector_type(8)));

typedef uint v2ui __attribute((ext_vector_type(2)));
typedef uint v8ui __attribute((ext_vector_type(8)));

typedef float v2sf __attribute((ext_vector_type(2)));
typedef float v4sf __attribute((ext_vector_type(4)));
typedef float v8sf __attribute((ext_vector_type(8)));

typedef half v4hf __attribute((ext_vector_type(4)));
typedef half v8hf __attribute((ext_vector_type(8)));
typedef half v16hf __attribute((ext_vector_type(16)));

typedef uint8 v8ub __attribute((ext_vector_type(8)));
typedef uint8 v32ub __attribute((ext_vector_type(32)));

inline v8si intX(int x) { return (v8si){x,x,x,x,x,x,x,x}; }
inline v8ui uintX(uint x) { return (v8ui){x,x,x,x,x,x,x,x}; }
inline v8si _0i = intX(0);
inline v8si _1i = intX(-1);

inline v4sf float4(float f) { return (v4sf){f,f,f,f}; }
inline constexpr v8sf float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
inline v8sf float8(v4sf a, v4sf b) { return __builtin_shufflevector(a, b, 0,1,2,3,4,5,6,7); }

//static constexpr v4sf _0011f = {0,0,1,1};
static constexpr v4sf _1100f = {1,1,0,0};
static constexpr v8sf _00001111f = {0,0,0,0,1,1,1,1};

static inline v2sf gather(const float* P, v2si i) { return {P[i[0]], P[i[1]]}; }
static inline v8sf gather(const float* P, v8ui i) { return __builtin_ia32_gatherd_ps256(_0i, P, i, _1i, sizeof(float)); }
static inline v8sf gather(const float* P, v8si i) { return __builtin_ia32_gatherd_ps256(_0i, P, i, _1i, sizeof(float)); }
static inline v8ui gather(const uint* P, v8si i) { return __builtin_ia32_gatherd_d256(_0i, reinterpret_cast<const int*>(P), i, _1i, sizeof(int)); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v8sf clamp(v8sf min, v8sf x, v8sf max) { return ::min(::max(min, x), max); }

static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }
static inline v8sf rsqrt(v8sf x) { return __builtin_ia32_rsqrtps256(x); }
static inline v8sf rcp(v8sf x) { return __builtin_ia32_rcpps256(x); }

struct v16si;

/// 16-wide vector operations using 2 v8sf AVX registers
struct v16sf {
    v8sf r1,r2;
    v16sf(){}
    explicit v16sf(float x){ r1 = r2 = float8(x);}
    v16sf(const v8sf& r1, const v8sf& r2) : r1(r1), r2(r2) {}
    v16sf(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7, float x8, float x9, float x10, float x11, float x12, float x13, float x14, float x15):r1((v8sf){x0,x1,x2,x3,x4,x5,x6,x7}),r2((v8sf){x8,x9,x10,x11,x12,x13,x14,x15}){}
    float& operator [](uint i) { return ((float*)this)[i]; }
    const float& operator [](uint i) const { return ((float*)this)[i]; }
    explicit operator v16si() const;
};

inline v16sf operator-(v16sf a) { return v16sf(-a.r1, -a.r2); }
inline v16sf& operator+=(v16sf& a, v16sf b) { a.r1 += b.r1; a.r2 += b.r2; return a; }
inline v16sf operator+(v16sf a, v16sf b) { return v16sf(a.r1 + b.r1, a.r2 + b.r2); }
inline v16sf operator-(v16sf a, v16sf b) { return v16sf(a.r1 - b.r1, a.r2 - b.r2); }
inline v16sf operator*(v16sf a, v16sf b) { return v16sf(a.r1 * b.r1, a.r2 * b.r2); }
inline v16sf operator/(v16sf a, v16sf b) { return v16sf(a.r1 / b.r1, a.r2 / b.r2); }
inline v16sf operator /(const int one unused, v16sf d) { assert(one==1); return v16sf(__builtin_ia32_rcpps256(d.r1),__builtin_ia32_rcpps256(d.r2)); }

//inline v16f shuffle() __builtin_shufflevector
#define shuffle(a, b, i0, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10, i11, i12, i13, i14, i15) \
 v16sf(__builtin_shufflevector(a, b, i0, i1, i2, i3, i4, i5, i6, i7), \
       __builtin_shufflevector(a, b, i8, i9, i10, i11, i12, i13, i14, i15))

inline float hsum(v4sf v) { // movshdup, addps, movhlps, addss
    v4sf t = v+__builtin_shufflevector(v,v, 1,1,3,3);
    return t[0]+t[2];
}
inline int hsum(v8si x) {
    const v4si sumQuad = __builtin_shufflevector(x, x, 0, 1, 2, 3) + __builtin_shufflevector(x, x, 4, 5, 6, 7); // 0 + 4, 1 + 5, 2 + 6, 3 + 7
    const v4si sumDual = sumQuad + __builtin_shufflevector(sumQuad, sumQuad, 2, 3, -1, -1); // 0+4 + 2+6, 1+5 + 3+7 (+movehl)
    return (sumDual + __builtin_shufflevector(sumDual, sumDual, 1, -1, -1, -1))[0]; // 0+4+2+6 + 1+5+3+7
}
inline float hsum(v8sf x) {
    const v4sf sumQuad = __builtin_shufflevector(x, x, 0, 1, 2, 3) + __builtin_shufflevector(x, x, 4, 5, 6, 7); // 0 + 4, 1 + 5, 2 + 6, 3 + 7
    const v4sf sumDual = sumQuad + __builtin_shufflevector(sumQuad, sumQuad, 2, 3, -1, -1); // 0+4 + 2+6, 1+5 + 3+7 (+movehl)
    return (sumDual + __builtin_shufflevector(sumDual, sumDual, 1, -1, -1, -1))[0]; // 0+4+2+6 + 1+5+3+7
}
inline float hsum(v16sf v) { return hsum(v.r1+v.r2); }

/// 16-wide vector operations using 2 v8si AVX registers
struct v16si {
    v8si r1,r2;
    int operator [](uint i) const { return ((int*)this)[i]; }
};

inline v16si operator~(v16si a) { return {~a.r1, ~a.r2}; }

static inline v16sf gather(const float* P, v16si i) { return v16sf(gather(P, i.r1), gather(P, i.r2)); }

inline v16si operator<(float a, v16sf b) { return {float8(a) < b.r1, float8(a) < b.r2}; }
inline v16si operator<=(float a, v16sf b) { return {float8(a) <= b.r1, float8(a) <= b.r2}; }
inline v16si operator>=(float a, v16sf b) { return {float8(a) >= b.r1, float8(a) >= b.r2}; }
inline v16si operator>(float a, v16sf b) { return {float8(a) > b.r1, float8(a) > b.r2}; }

inline v16si operator<(v16sf b, float a) { return a > b; }
inline v16si operator<=(v16sf b, float a) { return a >= b; }
inline v16si operator>=(v16sf b, float a) { return a <= b; }
inline v16si operator>(v16sf b, float a) { return a < b; }

inline v16si operator<(v16sf a, v16sf b) { return {a.r1 < b.r1, a.r2 < b.r2}; }
inline v16si operator<=(v16sf a, v16sf b) { return {a.r1 <= b.r1, a.r2 <= b.r2}; }
inline v16si operator>=(v16sf a, v16sf b) { return {a.r1 >= b.r1, a.r2 >= b.r2}; }
inline v16si operator>(v16sf a, v16sf b) { return {a.r1 > b.r1, a.r2 > b.r2}; }

inline mask8 mask(v8si m) { return __builtin_ia32_movmskps256(m); }
inline mask16 mask(v16si m) { return mask(m.r1)|(mask(m.r2)<<8); }

static const unused v8si selectMask {1<<0, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7};
inline v8si mask(const mask8 mask) { return (intX(mask) & selectMask) != _0i; }
inline v16si mask(const mask16 m) { return {mask(mask8(m)), mask(mask8(m>>8))}; }

inline v8sf and(v8si k, v8sf x) { return (v8sf)(k & (v8si)x); }
inline v8sf xor(v8si k, v8sf x) { return (v8sf)(k ^ (v8si)x); }
inline v16si operator &(v16si a, v16si b) { return {a.r1 & b.r1, a.r2 & b.r2}; }
//inline v16sf operator &(v16sf a, v16si b) { return v16sf(and(a.r1, b.r1), and(a.r2,b.r2)); }
inline v16sf and(v16si k, v16sf x) { return v16sf(and(k.r1, x.r1), and(k.r2,x.r2)); }

inline v8si blend(v8si A, v8si B, v8si mask) { return __builtin_ia32_blendvps256(A, B, mask); }
inline v8ui blend(v8ui A, v8ui B, v8si mask) { return __builtin_ia32_blendvps256(A, B, mask); }
inline v8sf blend(v8sf A, v8sf B, v8si mask) { return __builtin_ia32_blendvps256(A, B, mask); }
inline v16sf blend(v16sf A, v16sf B, v16si mask) { return v16sf(blend(A.r1, B.r1, mask.r1), blend(A.r2, B.r2, mask.r2)); }

inline void store(v8si& P, v8si M, v8si A) { __builtin_ia32_maskstoreps256((v8sf*)&P, M, (v8sf)A); }
inline void store(v8sf& P, v8si M, v8sf A) { __builtin_ia32_maskstoreps256(&P, M, A); }
inline void store(v16sf& P, v16si M, v16sf A) { store(P.r1, M.r1, A.r1); store(P.r2, M.r2, A.r2); }

inline v4sf floor(const v4sf v) { return __builtin_ia32_roundps(v, 1/*-∞*/); }
inline v8sf floor(const v8sf v) { return __builtin_ia32_roundps256(v, 1/*-∞*/); }
inline v16sf floor(const v16sf v) { return v16sf(floor(v.r1), floor(v.r2)); }

static unused const v4si notSignBit4 = {0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF};
inline v4sf abs(v4sf a) { return (v4sf)(notSignBit4 & (v4si)a); }

static unused const v8si notSignBit8 = {0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF};
inline v8sf abs(v8sf a) { return (v8sf)(notSignBit8 & (v8si)a); }

static unused const v8ui signBit8 = {0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000};
inline v8si sign(v8sf a) { return (v8si)signBit8 & (v8si)a; }

inline v8sf toFloat(const v8ui v) { return __builtin_ia32_cvtdq2ps256(v); }
inline v8sf toFloat(const v8si v) { return __builtin_ia32_cvtdq2ps256(v); }
inline v8hf toHalf(const v8sf v) { return __builtin_ia32_vcvtps2ph256(v, 0); }

inline v16hf toHalf(const v16sf v) { return __builtin_shufflevector(toHalf(v.r1), toHalf(v.r2), 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15); }

inline v4sf toFloat(const v4hf v) { return {(float)v[0], (float)v[1], (float)v[2], (float)v[3]}; }
inline v16sf toFloat(const v16hf v) {
    return v16sf(__builtin_ia32_vcvtph2ps256(__builtin_shufflevector(v, v, 0+0, 0+1, 0+2, 0+3, 0+4, 0+5, 0+6, 0+7)),
                 __builtin_ia32_vcvtph2ps256(__builtin_shufflevector(v, v, 8+0, 8+1, 8+2, 8+3, 8+4, 8+5, 8+6, 8+7)));
}

inline v8si cvtt(const v8sf v) { return __builtin_ia32_cvttps2dq256(v); }


inline float dot(v4sf a, v4sf b) { return __builtin_ia32_dpps(a, b, 0xFF)[0]; }
inline v4sf low(v8sf a) { return __builtin_shufflevector(a,a, 0,1,2,3); } // vextractf128 0
inline v4sf high(v8sf a) { return __builtin_shufflevector(a,a, 4,5,6,7); } // vextractf128 1
inline float dot(v8sf a, v8sf b) {
    v8sf dot4 = __builtin_ia32_dpps256(a, b, 0xFF);
    return (low(dot4)+high(dot4))[0];
}
inline float dot(v16sf a, v16sf b) { return dot(a.r1, b.r1) + dot(a.r2, b.r2); }

template<Type T, uint N> struct Vec { T _[N]; };

#include "math.h"
static inline Vec<v8sf, 2> cossin(const v8sf angle) {
  const v8sf absAngle = abs(angle);
  const v8si octant = (cvtt((4/PI) * absAngle)+1) & (~1);
  const v8si cosSign = ((~(octant-2))&4) << 29;
  const v8si swap = (octant&4) << 29;
  const v8si sinSign = swap ^ sign(angle);
  const v8sf y = toFloat(octant);
  const v8sf x = float8(-3.77489497744594108e-8) * y + (float8(-2.4187564849853515625e-4) * y + (float8(-0.78515625) * y + absAngle));
  const v8sf x2 = x*x;
  const v8sf y1 = (((float8(2.443315711809948e-5) * x2 + float8(-1.388731625493765e-3)) * x2 + float8(4.166664568298827e-2)) * x2 + float8(-1./2)) * x2 + 1; // 0 <= x <= PI/4
  const v8sf y2 = (((float8(-1.9515295891E-4) * x2 + float8(8.3321608736E-3)) * x2 + float8(-1.6666654611E-1)) * x2 + 1) * x; // -PI/4 <= x <= 0
  const v8si select = (octant&2) == 0;
  return {{xor(cosSign, blend(y1, y2, ~select)), xor(sinSign, blend(y1, y2, select))}};
}

template<> inline String str(const half& v) { return str(float(v)); }
template<> inline String str(const v8sf& v) { return "v8sf("_+str(ref<float>((float*)&v,8))+")"_; }
template<> inline String str(const v8si& v) { return "v8si("_+str(ref<int>((int*)&v,8))+")"_; }
template<> inline String str(const v16sf& v) { return "v16sf("_+str(ref<float>((float*)&v,16))+")"_; }
template<> inline String str(const v16si& v) { return "v16si("_+str(ref<int>((int*)&v,16))+")"_; }
