#pragma once
/// \file simd.h SIMD types and intrinsics
#include "core.h"

// AVX intrinsics
#define __AVX__ 1
#include "immintrin.h"
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include "avxintrin.h"
#endif

typedef float float2 __attribute((vector_size(8)));
typedef float float4 __attribute((vector_size(16)));
typedef float float8 __attribute((vector_size(32),may_alias));

inline float4 load4(const float* p) { return *(float4*)p; }
inline float4 load4(const float2* p) { return *(float4*)p; }
inline float4 load4u(const float* p) { return _mm_loadu_ps(p); }
inline float8 load8(const float* p) { return *(float8*)p; }
inline float8 load8(const float2* p) { return *(float8*)p; }
inline float8 load8u(const float* p) { return _mm256_loadu_ps(p); }
inline float8 load8u(const float2* p) { return _mm256_loadu_ps((float*)p); }

#if __clang__
inline float extract(float4 v, int i) { return v[i]; }
#else
#if DEBUG
#define extract(v,i) __builtin_ia32_vec_ext_v4sf(v,i)
#else
inline float extract(float4 v, int i) { return __builtin_ia32_vec_ext_v4sf(v,i); }
#endif
#endif

typedef int word8 __attribute((vector_size(32)));
inline word8 cvtps(float8 a) { return (word8)_mm256_cvtps_epi32(a); }
