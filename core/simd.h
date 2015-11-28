#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef uint v8ui __attribute((__vector_size__ (32)));
static inline v8ui gather(const buffer<uint>& B, v8ui a) {
 ref<uint> P (B.data, B.capacity); // Bounds check with aligned capacity
 return (v8ui){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}

typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static constexpr v8sf unused _0f = float8(0);
static constexpr v8sf unused _1f = float8(1);
static inline v8sf load(ref<float> a, size_t index) { return *(v8sf*)(a.data+index); }
static inline v8sf loadu(ref<float> a, size_t index) {
 struct v8sfu { v8sf v; } __attribute((__packed__, may_alias));
 return ((v8sfu*)(a.data+index))->v;
}
static inline void store(float* P, v8sf A) {   *(v8sf*)P = A; }
static inline void storeu(float* P, v8sf A) { __builtin_ia32_storeups256(P, A); }

inline v8sf sqrt8(v8sf x) { return __builtin_ia32_sqrtps256(x); }
static inline v8sf gather(const buffer<float>& B, v8ui a) {
 ref<float> P (B.data, B.capacity); // Bounds check with aligned capacity
 return (v8sf){P[a[0]], P[a[1]], P[a[2]], P[a[3]], P[a[4]], P[a[5]], P[a[6]], P[a[7]]};
}

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
