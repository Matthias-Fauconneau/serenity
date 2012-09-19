#pragma once
#include "core.h"

typedef short half8 __attribute((vector_size(16)));
typedef int word4 __attribute((vector_size(16)));
typedef float float2 __attribute((vector_size(8)));
typedef float float4 __attribute((vector_size(16)));

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
