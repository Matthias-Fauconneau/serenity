#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

#include <immintrin.h>
typedef float v4sf __attribute((__vector_size__ (16)));
typedef float v8sf __attribute((__vector_size__ (32)));
typedef float v16sf __attribute((__vector_size__ (64)));

inline v16sf float16(float f) { return (v16sf){f,f,f,f,f,f,f,f,f,f,f,f,f,f,f,f}; }
static v16sf unused _0f = float16(0);
static v16sf unused _1f = float16(1);

typedef int v16si __attribute((__vector_size__ (64)));

typedef uint16 mask16;

inline mask16 mask(v16sf A) { return __builtin_ia32_movmskps256(__builtin_shufflevector(A, A, 0,1,2,3,4,5,6,7))|
                                  (__builtin_ia32_movmskps256(__builtin_shufflevector(A, A, 8,9,10,11,12,13,14,15))<<8); }

inline v16sf and(v16sf A, v16si M) { return (v16sf)((v16si)A&M); }

static inline v16sf blend(mask16 k, v16sf a, v16sf b) { return _mm512_mask_blend_ps(k, a, b); }

inline void store(v16sf& P, v16sf A, mask16 M) { __builtin_ia32_storeaps512_mask(&P, A, M); }

inline float sum(v16sf A) {
    v16sf t = A + _mm512_shuffle_f32x4(A,A,_MM_SHUFFLE(0,0,3,2));
    v4sf r = _mm512_castps512_ps128(_mm512_add_ps(t,_mm512_shuffle_f32x4(t,t,_MM_SHUFFLE(0,0,0,1))));
    r = _mm_hadd_ps(r,r);
    return _mm_cvtss_f32(_mm_hadd_ps(r,r));
}
