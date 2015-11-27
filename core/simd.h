#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef uint16 v4hi __attribute((__vector_size__ (8)));
inline v4hi loada(const uint16* const ptr) { return *(v4hi*)ptr; }

// v4si
typedef int __attribute((__vector_size__(16))) v4si;
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
inline v4si loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline void storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }

// v4sf
typedef float __attribute((__vector_size__(16))) v4sf;
inline v4sf constexpr float3(float f) { return (v4sf){f,f,f,0.f}; }
inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }
static constexpr v4sf unused _0f4 = float4(0);
static constexpr v4sf unused _1f4 = float4(1);
static constexpr unused v4sf _0001f = {0,0,0,1};

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
inline v4sf dot2(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0b00111111); }
inline v4sf sq2(v4sf a) { return dot2(a,a); }
inline v4sf dot3(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0x7f); }
inline v4sf sq3(v4sf a) { return dot3(a,a); }
inline float sum3(v4sf a) { return dot3(a, _1f4)[0]; } // a0+a1+a2
inline v4sf dot4(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0xFF); }
inline v4sf sq4(v4sf a) { return dot4(a,a); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); }
inline v4si cvttps2dq(v4sf a) { return __builtin_ia32_cvttps2dq(a); }
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }

inline v4sf floor(v4sf a) { return __builtin_ia32_roundps(a, 0); }

inline v4sf mix(v4sf x, v4sf y, float a) { return float4(1-a)*x + float4(a)*y; }

#if __clang__
#define shuffle4(a,b, x, y, z, w) __builtin_shufflevector(a,b, x, y, z, w)
#else
#define shuffle4(a,b, x, y, z, w) (v4sf){a[x],a[y],b[z],b[w]}
#endif

inline v4sf cross(v4sf a, v4sf b) {
 return shuffle4(a, a, 1, 2, 0, 3) * shuffle4(b, b, 2, 0, 1, 3)
          - shuffle4(a, a, 2, 0, 1, 3) * shuffle4(b, b, 1, 2, 0, 3);
}

static constexpr unused v4si _0001 = {0,0,0,int(0x80000000)};
inline v4sf qmul(v4sf a, v4sf b) {
 // a3*b012 + b3*a012 + a012×b012, a3*b3 - a012·b012
 return shuffle4(a,a,3,3,3,3) * b - shuffle4(a,a,2,0,1,0) * shuffle4(b,b,1,2,0,0)
   + (v4sf)(_0001 ^ (v4si)(shuffle4(a,a,0,1,2,1) * shuffle4(b,b,3,3,3,1)
                                      + shuffle4(a,a,1,2,0,2) * shuffle4(b,b,2,0,1,2)));
}

typedef uint v8ui __attribute((__vector_size__ (32)));
/*static inline v8ui gather(ref<uint> P, v8ui a) {
 return v8ui{P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}*/
static inline v8ui gather(const buffer<uint>& B, v8ui a) {
 ref<uint> P (B.data, B.capacity); // Bounds check with aligned capacity
 return (v8ui){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}

#undef packed
#include <immintrin.h>
#define packed __attribute((packed))
typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static constexpr v8sf unused _0f = float8(0);
static constexpr v8sf unused _1f = float8(1);
#if __AVX__
static inline v8sf loadu8(const float* P) { return _mm256_loadu_ps(P); }
static inline void store(float* P, v8sf A) { _mm256_storeu_ps(P, A); }

inline v8sf sqrt8(v8sf x) { return __builtin_ia32_sqrtps256(x); }
static inline float reduce8(v8sf x) {
 const v4sf x128 = __builtin_ia32_vextractf128_ps256(x, 1) + _mm256_castps256_ps128(x);
 const __m128 x64 = x128 + _mm_movehl_ps/*__builtin_ia32_movhlps*/(x128, x128);
 const __m128 x32 = x64 + _mm_shuffle_ps/*__builtin_ia32_shufps*/(x64, x64, 0x55);
 return x32[0];
}
#endif
/*static inline v8sf gather(const float* P, v8ui a) {
 return (v8sf){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}
static inline v8sf gather(ref<float> P, v8ui a) {
 return (v8sf){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}*/
static inline v8sf gather(const buffer<float>& B, v8ui a) {
 ref<float> P (B.data, B.capacity); // Bounds check with aligned capacity
 return (v8sf){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}

/*static inline void scatter(mref<float> P, v8ui a, v8sf x) {
 P[a[0]] = x[0];
 P[a[1]] = x[1];
 P[a[2]] = x[2];
 P[a[3]] = x[3];
 P[a[4]] = x[4];
 P[a[5]] = x[5];
 P[a[6]] = x[6];
 P[a[7]] = x[7];
}*/

static inline void scatter(const buffer<float>& B, v8ui a, v8sf x) {
 mref<float> P (B.begin(), B.capacity); // Bounds check with aligned capacity
 P[a[0]] = x[0];
 P[a[1]] = x[1];
 P[a[2]] = x[2];
 P[a[3]] = x[3];
 P[a[4]] = x[4];
 P[a[5]] = x[5];
 P[a[6]] = x[6];
 P[a[7]] = x[7];
}

#include "math.h"
inline v4sf mean(const ref<v4sf> x) { assert(x.size); return sum(x, float4(0)) / float4(x.size); }
#include "string.h"
template<> inline String str(const v4sf& v) { return "("+str(v[0], v[1], v[2], v[3])+")"; }
inline bool isNumber(v4sf v){ for(uint i: range(4)) if(!isNumber(v[i])) return false; return true; }
template<> inline String str(const v8ui& v) {
 return "("+str(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])+")";
}
