#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

// v4si
typedef int v4si __attribute((__vector_size__ (16)));
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
inline v4si loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline v4si loadu(const uint32* const ptr) { return (v4si)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }

// v16qi
typedef byte v16qi  __attribute((__vector_size__ (16)));
inline v16qi loadu(const byte* const ptr) { return __builtin_ia32_lddqu(ptr); }
inline void storeu(byte* const ptr, v16qi a) { __builtin_ia32_storedqu(ptr, a); }

// v4sf
typedef float v4sf __attribute((__vector_size__ (16)));
inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline v4sf loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); }
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }
