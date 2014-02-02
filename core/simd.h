#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

// v4si
typedef int v4si __attribute((__vector_size__ (16)));
unused const v4si _1i = {1,1,1,1};
inline v4si set1(int i) { return (v4si){i,i,i,i}; }
inline v4si loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline v4si loadu(const uint32* const ptr) { return (v4si)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }

// v8hi
typedef short v8hi __attribute((__vector_size__ (16)));
unused const v8hi _0h = {0,0,0,0};
inline v8hi short8(int16 i) { return (v8hi){i,i,i,i,i,i,i,i}; }
inline v8hi loada(const uint16* const ptr) { return *(v8hi*)ptr; }
inline v8hi loadu(const uint16* const ptr) { return (v8hi)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(uint16* const ptr, v8hi a) { *(v8hi*)ptr = a; }
inline v8hi packss(v4si a, v4si b) { return __builtin_ia32_packssdw128(a,b); }
inline v8hi shiftRight(v8hi a, uint imm) { return __builtin_ia32_psrlwi128(a, imm); }
inline v8hi select(v8hi a, v8hi b, v8hi mask) { return (b & mask) | (a & ~mask); }
inline v8hi min(v8hi a, v8hi b) { return select(a, b, b<a); }
inline v8hi max(v8hi a, v8hi b) { return select(a, b, b>a); }

// v16qi
typedef byte v16qi  __attribute((__vector_size__ (16)));
inline v16qi loadu(const byte* const ptr) { return __builtin_ia32_lddqu(ptr); }
inline void storeu(byte* const ptr, v16qi a) { __builtin_ia32_storedqu(ptr, a); }

// v4sf
typedef float v4sf __attribute((__vector_size__ (16)));
inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }
unused const v4sf _1f = float4( 1 );
unused const v4sf _0f = float4( 0 );

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline v4sf loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

//inline v4sf bitOr(v4sf a, v4sf b) { return __builtin_ia32_orps(a, b); }
//inline v4sf andnot(v4sf a, v4sf b) { return __builtin_ia32_andnps(a, b); }
//inline v4sf bitXor(v4sf a, v4sf b) { return __builtin_ia32_xorps(a, b); }

const v4sf signBit = (v4sf)(v4si){(int)0x80000000,(int)0x80000000,(int)0x80000000,(int)0x80000000};
//inline v4sf negate(v4sf a) { return bitXor(a,  signBit); }
//inline v4sf abs(v4sf a) { return andnot(signBit, a); }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
//inline v4sf shuffle(v4sf a, v4sf b, int x, int y, int z, int w) { return __builtin_ia32_shufps(a, b, w<<6|z<<4|y<<2|x); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }
//inline v4sf transpose(v4sf a, v4sf b, v4sf c, v4sf d) { return shuffle(shuffle(a,b,0,0,0,0), shuffle(c,d,0,0,0,0),0,2,0,2); }
//inline v4sf hmin(v4sf a) { a = min(a, shuffle(a, a, 1,0,3,2)); return min(a, shuffle(a, a, 2,2,0,0)); }
//inline v4sf hmax(v4sf a) { a = max(a, shuffle(a, a, 1,0,3,2)); return max(a, shuffle(a, a, 2,2,0,0)); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); }
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }
