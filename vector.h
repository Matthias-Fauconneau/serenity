#pragma once
#include "string.h"

/// Mathematic primitives

template<class T> inline T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
/*
inline int floor(float f) { return __builtin_floorf(f); }
inline int round(float f) { return __builtin_roundf(f); }
inline int ceil(float f) { return __builtin_ceilf(f); }

const double PI = 3.14159265358979323846;
inline float sin(float t) { return __builtin_sinf(t); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float atan(float f) { return __builtin_atanf(f); }

template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }

/// SIMD
typedef float float4 __attribute__ ((vector_size(16)));
typedef double double2 __attribute__ ((vector_size(16)));
#define xor_ps __builtin_ia32_xorps
#define xor_pd __builtin_ia32_xorpd
#define loadu_ps __builtin_ia32_loadups
#define loadu_pd __builtin_ia32_loadupd
#define loada_ps(e) (*(float4*)(e))
#define movehl_ps __builtin_ia32_movhlps
#define shuffle_ps __builtin_ia32_shufps
#define extract_s __builtin_ia32_vec_ext_v4sf
#define extract_d __builtin_ia32_vec_ext_v2df
*/

#define __ARM_NEON__
#include <arm_neon.h>
union int2 {
    int32x2_t v;
    struct { int x,y; };
    int2():v{0,0}{}
    int2(int s):x(s),y(s){}
    int2(int x, int y):x(x),y(y){}
    int2(int32x2_t v):v(v){}
    operator int32x2_t&() { return v; }
    operator const int32x2_t&() const { return v; }
    explicit operator bool() { return x!=0||y!=0; }
};
int2 min(int2 a, int2 b) { return vmin_s32(a,b); }
int2 max(int2 a, int2 b) { return vmax_s32(a,b); }
int2 abs(int2 a) { return vabs_s32(a); }

union byte4 {
    typedef uint8 __attribute((vector_size(4))) vec;
    vec v;
    struct { byte b,g,r,a; };
    operator vec&() { return v; }
    operator const vec&() const { return v; }
};

typedef int int4 __attribute((vector_size(16)));
