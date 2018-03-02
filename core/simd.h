#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef uint8 mask8;
typedef uint16 mask16;

typedef uint64 v4uq __attribute((ext_vector_type(4)));

typedef int32 v4si __attribute((ext_vector_type(4)));
typedef int32 v8si __attribute((ext_vector_type(8)));
typedef uint32 v8ui __attribute((ext_vector_type(8)));

typedef int16 v16hi __attribute((ext_vector_type(16)));

typedef float v8sf __attribute((ext_vector_type(8)));

typedef uint8 v16ub __attribute((ext_vector_type(16)));
typedef uint8 v32ub __attribute((ext_vector_type(32)));

static constexpr v8si int32x8(int32 x) { return (v8si){x,x,x,x,x,x,x,x}; }
static constexpr v8sf float32x8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static constexpr v16hi int16x16(int16 x) { return (v16hi){x,x,x,x,x,x,x,x,x,x,x,x,x,x,x,x}; }

/// 16-wide vector operations using 2 v8si AVX registers
struct v16si {
    v8si r1, r2;
    v16si() {}
    explicit v16si(int32 x):r1{int32x8(x)},r2{int32x8(x)}{}
    constexpr v16si(const v8si& r1, const v8si& r2) : r1(r1), r2(r2) {}
    constexpr v16si(int32 x0, int32 x1, int32 x2, int32 x3, int32 x4, int32 x5, int32 x6, int32 x7, int32 x8,
                    int32 x9, int32 x10, int32 x11, int32 x12, int32 x13, int32 x14, int32 x15)
        : r1((v8si){x0,x1,x2,x3,x4,x5,x6,x7}),
          r2((v8si){x8,x9,x10,x11,x12,x13,x14,x15}){}
    int32 operator[](uint i) const { return ((int32*)this)[i]; }
    int32& operator[](uint i) { return ((int32*)this)[i]; }
};

/// 16-wide vector operations using 2 v8sf AVX registers
struct v16sf {
    v8sf r1, r2;
    v16sf() {}
    explicit v16sf(float x):r1{float32x8(x)},r2{float32x8(x)}{}
    constexpr v16sf(const v8sf& r1, const v8sf& r2) : r1(r1), r2(r2) {}
    float operator[](uint i) const { return ((float*)this)[i]; }
    float& operator[](uint i) { return ((float*)this)[i]; }
    explicit operator v16si() const { return v16si((v8si)r1, (v8si)r2); }
};

static inline v16si int32x16(int32 x) { return v16si(x); }

static inline v8si load8(const int32* A, uint i) { return *(v8si*)(A+i); }
static inline v8si load8(ref<int32> A, uint i) { return load8(A.begin(), i); }

static inline v8sf load8(const float32* A, uint i) { return *(v8sf*)(A+i); }
static inline v8sf load8(ref<float32> A, uint i) { return load8(A.begin(), i); }

static inline v16si load16(const int32* A, uint i) { return *(v16si*)(A+i); }
static inline v16si load16(ref<int32> A, uint i) { return load16(A.begin(), i); }

static inline v16sf load16(const float32* A, uint i) { return *(v16sf*)(A+i); }
static inline v16sf load16(ref<float32> A, uint i) { return load16(A.begin(), i); }

static inline void store(uint32* A, uint i, v8ui v) { *(v8ui*)(A+i) = v; }
static inline void store(mref<uint32> A, uint i, v8ui v) { store(A.begin(), i, v); }

static inline void store(int32* A, uint i, v16si v) { *(v16si*)(A+i) = v; }
static inline void store(mref<int32> A, uint i, v16si v) { store(A.begin(), i, v); }

static inline void store(float* A, uint i, v8sf v) { *(v8sf*)(A+i) = v; }
static inline void store(mref<float> A, uint i, v8sf v) { store(A.begin(), i, v); }

static inline v16si& operator|=(v16si& a, v16si b) { a.r1 |= b.r1; a.r2 |= b.r2; return a; }
static inline v16si& operator+=(v16si& a, v16si b) { a.r1 += b.r1; a.r2 += b.r2; return a; }

static constexpr v8si _0i = int32x8(0);
static constexpr v8si _1i = int32x8(-1);
static constexpr v8si seqI {0, 1, 2, 3, 4, 5, 6, 7};
//static constexpr v16si seqI (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

static inline v8si gather(const int32* P, v8si i) { return __builtin_ia32_gatherd_d256(_0i, P, i, _1i, sizeof(int32)); }

static inline v16si gather(const int32* P, v16si i) { return v16si(gather(P, i.r1), gather(P, i.r2)); }
static inline v16si gather(const ref<int32> P, v16si i) { return gather(P.begin(), i); }

static inline v8sf gather(const float32* P, v8si i) { return __builtin_ia32_gatherd_ps256(_0i, P, i, _1i, sizeof(float32)); }
static inline v8sf gather(const ref<float32> P, v8si i) { return gather(P.begin(), i); }

static inline v16sf gather(const float32* P, v16si i) { return v16sf(gather(P, i.r1), gather(P, i.r2)); }
static inline v16sf gather(const ref<float32> P, v16si i) { return gather(P.begin(), i); }

static inline v16si operator&(v16si a, v16si b) { return {a.r1 & b.r1, a.r2 & b.r2}; }
static inline v16si operator|(v16si a, v16si b) { return {a.r1 | b.r1, a.r2 | b.r2}; }
static inline v16si operator>>(v16si a, uint b) { return {a.r1 >> b, a.r2 >> b}; }
static inline v16si operator<<(v16si a, uint b) { return {a.r1 << b, a.r2 << b}; }

static inline v16si operator~(v16si a) { return v16si(~a.r1, ~a.r2); }
static inline v16si operator-(v16si a) { return v16si(-a.r1, -a.r2); }

static inline v16si operator==(v16si a, v16si b) { return {a.r1 == b.r1, a.r2 == b.r2}; }
static inline v16si operator!=(v16si a, v16si b) { return {a.r1 != b.r1, a.r2 != b.r2}; }
static inline v16si operator<(v16si a, v16si b) { return {a.r1 < b.r1, a.r2 < b.r2}; }
static inline v16si operator<=(v16si a, v16si b) { return {a.r1 <= b.r1, a.r2 <= b.r2}; }
static inline v16si operator>=(v16si a, v16si b) { return {a.r1 >= b.r1, a.r2 >= b.r2}; }
static inline v16si operator>(v16si a, v16si b) { return {a.r1 > b.r1, a.r2 > b.r2}; }

static inline v16si operator+(v16si a, v16si b) { return v16si(a.r1 + b.r1, a.r2 + b.r2); }
static inline v16si operator-(v16si a, v16si b) { return v16si(a.r1 - b.r1, a.r2 - b.r2); }
static inline v16si operator*(v16si a, v16si b) { return v16si(a.r1 * b.r1, a.r2 * b.r2); }
static inline v16si operator/(v16si a, v16si b) { return v16si(a.r1 / b.r1, a.r2 / b.r2); }

static inline v8si min(v8si a, v8si b) { return __builtin_ia32_pminsd256(a, b); }
static inline v16si min(v16si a, v16si b) { return v16si(min(a.r1, b.r1), min(a.r2, b.r2)); }

static inline v8si max(v8si a, v8si b) { return __builtin_ia32_pmaxsd256(a, b); }
static inline v16si max(v16si a, v16si b) { return v16si(max(a.r1, b.r1), max(a.r2, b.r2)); }

static inline v8si sign(v8si a, v8si b) { return __builtin_ia32_psignd256(a, b); }
static inline v16si sign(v16si a, v16si b) { return {sign(a.r1, b.r1), sign(a.r2, b.r2)}; }

static inline v8sf sign(v8sf a, v8si b) { return (v8sf)((v8si)a ^ b); }
static inline v16sf sign(v16sf a, v16si b) { return {sign(a.r1, b.r1), sign(a.r2, b.r2)}; }

static inline mask8 mask(v8si m) { return __builtin_ia32_movmskps256(m); }

static inline mask16 mask(v16ub m) { return __builtin_ia32_pmovmskb128(m); }
static inline mask16 mask(v16hi m) { return mask(__builtin_shufflevector((v32ub)m,(v32ub)m,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30)); }
static inline mask16 mask(v16si m) { return mask(m.r1)|(mask(m.r2)<<8); }

inline bool allZero(v8si m) { return __builtin_ia32_ptestz256(m, m); }

static constexpr v8si notSignBit8 = {0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF};
inline v8sf abs(v8sf a) { return (v8sf)(notSignBit8 & (v8si)a); }

static constexpr v8ui signBit8 = {0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000};
static constexpr v16si signBit16 {(v8si)signBit8, (v8si)signBit8};
inline v8si sign(v8sf a) { return (v8si)signBit8 & (v8si)a; }

inline v8sf xor(v8si k, v8sf x) { return (v8sf)(k ^ (v8si)x); }

inline v8sf blend(v8sf A, v8sf B, v8si mask) { return __builtin_ia32_blendvps256(A, B, mask); }

// Float

static inline v16sf& operator+=(v16sf& a, v16sf b) { a.r1 += b.r1; a.r2 += b.r2; return a; }

static inline v16si operator==(v16sf a, v16sf b) { return {a.r1 == b.r1, a.r2 == b.r2}; }
static inline v16si operator!=(v16sf a, v16sf b) { return {a.r1 != b.r1, a.r2 != b.r2}; }
static inline v16si operator<(v16sf a, v16sf b) { return {a.r1 < b.r1, a.r2 < b.r2}; }
static inline v16si operator<=(v16sf a, v16sf b) { return {a.r1 <= b.r1, a.r2 <= b.r2}; }
static inline v16si operator>=(v16sf a, v16sf b) { return {a.r1 >= b.r1, a.r2 >= b.r2}; }
static inline v16si operator>(v16sf a, v16sf b) { return {a.r1 > b.r1, a.r2 > b.r2}; }

static inline v16sf operator-(v16sf a) { return v16sf(-a.r1, -a.r2); }
static inline v16sf operator+(v16sf a, v16sf b) { return v16sf(a.r1 + b.r1, a.r2 + b.r2); }
static inline v16sf operator-(v16sf a, v16sf b) { return v16sf(a.r1 - b.r1, a.r2 - b.r2); }
static inline v16sf operator*(v16sf a, v16sf b) { return v16sf(a.r1 * b.r1, a.r2 * b.r2); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v16sf min(v16sf a, v16sf b) { return v16sf(min(a.r1, b.r1), min(a.r2, b.r2)); }

static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v16sf max(v16sf a, v16sf b) { return v16sf(max(a.r1, b.r1), max(a.r2, b.r2)); }

static inline v16sf operator&(v16si a, v16sf b) { return {(v8sf)(a.r1 & (v8si)b.r1), (v8sf)(a.r2 & (v8si)b.r2)}; }

static inline v8sf rcp(v8sf x) { return __builtin_ia32_rcpps256(x); }
static inline v16sf rcp(v16sf x) { return v16sf(rcp(x.r1), rcp(x.r2)); }

static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }

static inline v16sf operator/(v16sf a, v16sf b) { return v16sf(a.r1 / b.r1, a.r2 / b.r2); }

static inline v8si cvt(const v8sf v) { return __builtin_ia32_cvtps2dq256(v); }
static inline v16si cvt(const v16sf v) { return {cvt(v.r1), cvt(v.r2)}; }

static inline v8si cvtt(const v8sf v) { return __builtin_ia32_cvttps2dq256(v); }
static inline v16si cvtt(const v16sf v) { return {cvtt(v.r1), cvtt(v.r2)}; }

static inline v8sf toFloat(const v8si v) { return __builtin_ia32_cvtdq2ps256(v); }
static inline v16sf toFloat(const v16si v) { return {toFloat(v.r1), toFloat(v.r2)}; }

inline int32 hsum(v8si x) {
    const v4si sumQuad = __builtin_shufflevector(x, x, 0, 1, 2, 3) + __builtin_shufflevector(x, x, 4, 5, 6, 7); // 0 + 4, 1 + 5, 2 + 6, 3 + 7
    const v4si sumDual = sumQuad + __builtin_shufflevector(sumQuad, sumQuad, 2, 3, -1, -1); // 0+4 + 2+6, 1+5 + 3+7 (+movehl)
    return (sumDual + __builtin_shufflevector(sumDual, sumDual, 1, -1, -1, -1))[0]; // 0+4+2+6 + 1+5+3+7
}
inline int32 hsum(v16si v) { return hsum(v.r1+v.r2); }

template<Type T, uint N> struct Vec { T _[N]; };
