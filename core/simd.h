#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef float v4sf __attribute((ext_vector_type(4)));

inline v4sf loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }

inline float hsum(v4sf v) { // movshdup, addps, movhlps, addss
    v4sf t = v+__builtin_shufflevector(v,v, 1,1,3,3);
    return t[0]+t[2];
}
