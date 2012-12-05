#pragma once
/// \file simd.h SIMD types and intrinsics
#include "core.h"

// AVX intrinsics
#define __AVX__ 1
#include "immintrin.h"
#ifndef __GXX_EXPERIMENTAL_CXX0X__ //for QtCreator
#include "avxintrin.h"
#endif

typedef short half8 __attribute((vector_size(16)));
typedef int word4 __attribute((vector_size(16)));
typedef float float2 __attribute((vector_size(8)));
typedef float float4 __attribute((vector_size(16)));
typedef float float8 __attribute((vector_size(32),may_alias));

inline half8 packs(word4 a, word4 b) { return __builtin_ia32_packssdw128(a,b); }
inline word4 cvtps(float4 a) { return __builtin_ia32_cvtps2dq(a); }
inline float4 load(const float* p) { return *(float4*)p; }
inline float4 load(const float2* p) { return *(float4*)p; }
inline word4 sra(word4 v, int i) { return __builtin_ia32_psradi128(v,i); }
#if __clang__
inline float extract(float4 v, int i) { return v[i]; }
#else
#if DEBUG
#define extract(v,i) __builtin_ia32_vec_ext_v4sf(v,i)
#else
inline float extract(float4 v, int i) { return __builtin_ia32_vec_ext_v4sf(v,i); }
#endif
#endif

inline __m128 sum8to2(float8 x) {
    // hiQuad = ( x7, x6, x5, x4 )
    const __m128 hiQuad = _mm256_extractf128_ps(x, 1);
    // loQuad = ( x3, x2, x1, x0 )
    const __m128 loQuad = _mm256_castps256_ps128(x);
    // sumQuad = ( x3 + x7, x2 + x6, x1 + x5, x0 + x4 )
    const __m128 sumQuad = _mm_add_ps(loQuad, hiQuad);
    // loDual = ( -, -, x1 + x5, x0 + x4 )
    const __m128 loDual = sumQuad;
    // hiDual = ( -, -, x3 + x7, x2 + x6 )
    const __m128 hiDual = _mm_movehl_ps(sumQuad, sumQuad);
    // sumDual = ( -, -, x1 + x3 + x5 + x7, x0 + x2 + x4 + x6 )
    const __m128 sumDual = _mm_add_ps(loDual, hiDual);
    return sumDual;
}
