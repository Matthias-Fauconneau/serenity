#pragma once
/// \file simd.h SIMD types and intrinsics
#include "core.h"

#include <xmmintrin.h>

typedef float float2 __attribute((vector_size(8)));

typedef float float4 __attribute((vector_size(16)));
inline float4 load4(const float* p) { return *(float4*)p; }
inline float4 load4u(const float* p) { return _mm_loadu_ps(p); }

#if __clang__
inline float extract(float4 v, int i) { return v[i]; }
#else
#if DEBUG
#define extract(v,i) __builtin_ia32_vec_ext_v4sf(v,i)
#else
inline float extract(float4 v, int i) { return __builtin_ia32_vec_ext_v4sf(v,i); }
#endif
#endif

typedef int word4 __attribute((vector_size(16)));
inline word4 cvtps(float4 a) { return (word4)_mm_cvtps_epi32(a); }

// AVX intrinsics
//#if __AVX__
#if 0
#include "immintrin.h"
#include "avxintrin.h"

typedef float float8 __attribute((vector_size(32),may_alias));
inline float8 load8(const float* p) { return *(float8*)p; }
inline float8 load8u(const float* p) { return _mm256_loadu_ps(p); }

typedef int word8 __attribute((vector_size(32)));
inline word8 cvtps(float8 a) { return (word8)_mm256_cvtps_epi32(a); }
#endif
