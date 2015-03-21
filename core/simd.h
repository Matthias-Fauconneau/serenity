#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

// v4si
typedef int __attribute((__vector_size__(16))) v4si;
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
inline v4si loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline v4si loadu(const uint32* const ptr) { return (v4si)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }

// v4sf
typedef float __attribute((__vector_size__(16))) v4sf;
inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }
static constexpr v4sf unused _1f = float4( 1 );

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline v4sf loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
inline v4sf dot3(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0x7f); }
inline float sum3(v4sf a) { return dot3(a, _1f)[0]; } // a0+a1+a2
inline v4sf sq3(v4sf a) { return dot3(a,a); }
inline v4sf dot(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0xFF); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); }
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }

inline v4sf mix(v4sf x, v4sf y, float a) { return float4(1-a)*x + float4(a)*y; }

#include "math.h"
inline v4sf mean(const ref<v4sf> x) { assert(x.size); return sum(x, float4(0)) / float4(x.size); }
#include "string.h"
template<> inline String str(const v4sf& v) { return "("+str(v[0], v[1], v[2], v[3])+")"; }

#undef inline
