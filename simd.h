#pragma once
/// \file simd.h SIMD types and intrinsics
#include "core.h"

#include <xmmintrin.h>

typedef float float2 __attribute((vector_size(8)));

typedef float float4 __attribute((vector_size(16)));
inline float4 load4(const float* p) { return *(float4*)p; }
inline float4 load4u(const float* p) { return _mm_loadu_ps(p); }

// AVX intrinsics
#if __AVX__
#include "immintrin.h"
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include "avxintrin.h"
#endif

typedef float float8 __attribute((vector_size(32),may_alias));
inline float8 load8(const float* p) { return *(float8*)p; }
inline float8 load8u(const float* p) { return _mm256_loadu_ps(p); }

typedef int word8 __attribute((vector_size(32)));
inline word8 cvtps(float8 a) { return (word8)_mm256_cvtps_epi32(a); }
#endif
