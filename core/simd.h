#pragma once
/// \file simd.h SIMD intrinsics
#include "core.h"

typedef uint8 mask8;

typedef int32 __attribute((ext_vector_type(8))) v8si;
typedef uint32 __attribute((ext_vector_type(8))) v8ui;

typedef float32 __attribute((ext_vector_type(4))) v4sf;
typedef float32 __attribute((ext_vector_type(8))) v8sf;

static constexpr v8si int32x8(int32 x) { return (v8si){x,x,x,x,x,x,x,x}; }
static constexpr v8sf float32x8(float32 f) { return (v8sf){f,f,f,f,f,f,f,f}; }

static inline v8si load8(const int32* A, uint i) { return *(v8si*)(A+i); }
static inline v8si load8(ref<int32> A, uint i) { return load8(A.begin(), i); }

static inline v8sf load8(const float32* A, uint i) { return *(v8sf*)(A+i); }
static inline v8sf load8(ref<float32> A, uint i) { return load8(A.begin(), i); }

static inline void store(uint32* A, uint i, v8ui v) { *(v8ui*)(A+i) = v; }
static inline void store(mref<uint32> A, uint i, v8ui v) { store(A.begin(), i, v); }

static inline void store(float32* A, uint i, v8sf v) { *(v8sf*)(A+i) = v; }
static inline void store(mref<float32> A, uint i, v8sf v) { store(A.begin(), i, v); }

static constexpr v8si _0i = int32x8(0);
static constexpr v8si _1i = int32x8(-1);
static constexpr v8si seqI {0, 1, 2, 3, 4, 5, 6, 7};

static inline v8si gather(const int32* P, v8si i) { return __builtin_ia32_gatherd_d256(_0i, P, i, _1i, sizeof(int32)); }

static inline v8sf gather(const float32* P, v8si i) { return __builtin_ia32_gatherd_ps256(_0i, P, i, _1i, sizeof(float32)); }
static inline v8sf gather(const ref<float32> P, v8si i) { return gather(P.begin(), i); }

static inline v8si min(v8si a, v8si b) { return __builtin_ia32_pminsd256(a, b); }
static inline v8si max(v8si a, v8si b) { return __builtin_ia32_pmaxsd256(a, b); }
static inline v8si sign(v8si a, v8si b) { return __builtin_ia32_psignd256(a, b); }
static inline mask8 mask(v8si m) { return __builtin_ia32_movmskps256(m); }

static inline bool allZero(v8si m) { return __builtin_ia32_ptestz256(m, m); }

// Float

static constexpr v8si notSignBit8 = {0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF,0x7FFFFFFF};
static inline v8sf abs(v8sf a) { return (v8sf)(notSignBit8 & (v8si)a); }

static constexpr v8ui signBit8 = {0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000,0x80000000};
static inline v8si sign(v8sf a) { return (v8si)signBit8 & (v8si)a; }

static inline v8sf xor(v8si k, v8sf x) { return (v8sf)(k ^ (v8si)x); }
static inline v8sf sign(v8sf a, v8si b) { return (v8sf)((v8si)a ^ b); }

static inline v8sf and(v8si C, v8sf T) { return (v8sf)(C & (v8si)T); }
static inline v8sf select(v8si C, v8sf T, v8sf F) { return __builtin_ia32_blendvps256(T, F, C); }

static inline v8sf min(v8sf a, v8sf b) { return __builtin_ia32_minps256(a, b); }
static inline v8sf max(v8sf a, v8sf b) { return __builtin_ia32_maxps256(a, b); }
static inline v8sf rcp(v8sf x) { return __builtin_ia32_rcpps256(x); }
static inline v8sf sqrt(v8sf x) { return __builtin_ia32_sqrtps256(x); }
static inline v8si cvt(const v8sf v) { return __builtin_ia32_cvtps2dq256(v); }
static inline v8si cvtt(const v8sf v) { return __builtin_ia32_cvttps2dq256(v); }
static inline v8sf toFloat(const v8si v) { return __builtin_ia32_cvtdq2ps256(v); }

static inline float32 hsum(v8sf x) {
    const v4sf sumQuad = __builtin_shufflevector(x, x, 0, 1, 2, 3) + __builtin_shufflevector(x, x, 4, 5, 6, 7); // 0 + 4, 1 + 5, 2 + 6, 3 + 7
    const v4sf sumDual = sumQuad + __builtin_shufflevector(sumQuad, sumQuad, 2, 3, -1, -1); // 0+4 + 2+6, 1+5 + 3+7 (+movehl)
    return (sumDual + __builtin_shufflevector(sumDual, sumDual, 1, -1, -1, -1))[0]; // 0+4+2+6 + 1+5+3+7
}
