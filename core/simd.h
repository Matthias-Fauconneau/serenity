#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"
#if __GNUC__
#define inline(R) extern __inline R __attribute__((__gnu_inline__, __always_inline__, __artificial__))
#else
#define inline(R) static __inline R __attribute__((__always_inline__, __nodebug__))
#endif
// v2hi
typedef short v2hi __attribute((__vector_size__ (4)));

// v4si
typedef int v4si __attribute((__vector_size__ (16)));
unused const v4si _1i = {1,1,1,1};
inline (v4si) set1(int i) { return (v4si){i,i,i,i}; }
inline (v4si) loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline (v4si) loadu(const uint32* const ptr) { return (v4si)__builtin_ia32_lddqu((byte*)ptr); }
inline (void) storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }

// v8hi
typedef short v8hi __attribute((__vector_size__ (16)));
unused const v8hi _0h = {0,0,0,0};
inline (v8hi) short8(int16 i) { return (v8hi){i,i,i,i,i,i,i,i}; }
inline (v8hi) loada(const uint16* const ptr) { return *(v8hi*)ptr; }
inline (v8hi) loadu(const uint16* const ptr) { return (v8hi)__builtin_ia32_lddqu((byte*)ptr); }
inline (void) storea(uint16* const ptr, v8hi a) { *(v8hi*)ptr = a; }
inline (v8hi) packss(v4si a, v4si b) { return __builtin_ia32_packssdw128(a,b); }
inline (v8hi) shiftRight(v8hi a, uint imm) { return __builtin_ia32_psrlwi128(a, imm); }
inline (v8hi) select(v8hi a, v8hi b, v8hi mask) { return (b & mask) | (a & ~mask); }
inline (v8hi) min(v8hi a, v8hi b) { return select(a, b, b<a); }
inline (v8hi) max(v8hi a, v8hi b) { return select(a, b, b>a); }

// v16qi
typedef byte v16qi  __attribute((__vector_size__ (16)));
inline (v16qi) loadu(const byte* const ptr) { return __builtin_ia32_lddqu(ptr); }
inline (void) storeu(byte* const ptr, v16qi a) { __builtin_ia32_storedqu(ptr, a); }

// v4sf
typedef float v4sf __attribute((__vector_size__ (16)));
inline (v4sf) constexpr float4(float f) { return (v4sf){f,f,f,f}; }
inline (v4sf) constexpr float4(float a, float b, float c, float d) { return (v4sf){a,b,c,d}; }
unused const v4sf _1f = float4( 1 );
unused const v4sf _0f = float4( 0 );

inline (v4sf) loada(const float* const ptr) { return *(v4sf*)ptr; }
inline (v4sf) loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }
inline (void) storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline (v4sf) bitOr(v4sf a, v4sf b) { return v4sf(v4si(a) | v4si(b)); } //__builtin_ia32_orps(a, b); }

const v4si notSignBit4 = (v4si){(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF};
inline (v4sf) abs(v4sf a) { return v4sf(notSignBit4 & v4si(a)); }

inline (v4sf) min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline (v4sf) max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
#if __GNUC__
#define shuffle4(v1, v2, i1, i2, i3, i4) __builtin_shuffle(v1,v2, (v4si){i1,i2, 4+(i3), 4+(i4)})
#else
#define shuffle4(v1, v2, i1, i2, i3, i4) __builtin_shufflevector(v1,v2, i1,i2, 4+(i3), 4+(i4))
#endif
inline (v4sf) hadd(v4sf a, v4sf b) { return __builtin_ia32_haddps(a,b); } //a0+a1, a2+a3, b0+b1, b2+b3
inline (v4sf) dot4(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0xFF); }
inline (v4sf) hsum(v4sf a) { return dot4(a,_1f); }
inline (v4sf) rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline (v4sf) rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline (v4sf) sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline (int) mask(v4sf a) { return __builtin_ia32_movmskps(a); }
#define blendps __builtin_ia32_blendps
inline (v4sf) blendv(v4sf a, v4sf b, v4sf m) { return __builtin_ia32_blendvps(a, b, m); }
inline (v4sf) dot2(v4sf a, v4sf b) { v4sf sq = a*b; return hadd(sq,sq); }
inline (v4sf) hmin(v4sf a) { a = min(a, shuffle4(a, a, 1,0,3,2)); return min(a, shuffle4(a, a, 2,2,0,0)); }
inline (v4sf) hmax(v4sf a) { a = max(a, shuffle4(a, a, 1,0,3,2)); return max(a, shuffle4(a, a, 2,2,0,0)); }

inline (v4si) cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); } // Rounds
inline (v4si) cvttps2dq(v4sf a) { return __builtin_ia32_cvttps2dq(a); } // Truncates
inline (v4sf) cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }

typedef int v8si __attribute((__vector_size__ (32)));
const v8si _00000000 = (v8si){0,0,0,0, 0,0,0,0};
const v8si _1111111 = (v8si){~0ll,~0ll,~0ll,~0ll, ~0ll,~0ll,~0ll,~0ll};

typedef float v8sf __attribute((__vector_size__ (32)));
const v8sf _00000000f = (v8sf){0,0,0,0, 0,0,0,0};

#if __GNUC__
inline (v8sf) gather(const float* base, v8si indices) { return __builtin_ia32_gathersiv8sf(_00000000f, base, indices, (v8sf)_1111111, 4); }
#else
inline (v8sf) gather(const float* base, v8si indices) { return __builtin_ia32_gatherd_ps256(_00000000f, (v8sf*)base, indices, _1111111, 4); }
#endif

inline (v8sf) float8(float f) { return (v8sf){f,f,f,f, f,f,f,f}; }
inline (v8sf) constexpr float8(float a, float b, float c, float d, float e, float f, float g, float h) { return (v8sf){a,b,c,d,e,f,g,h}; }

typedef long long int v2di __attribute((__vector_size__ (16)));

inline (v8sf) dup(v4sf a) { return (v8sf)__builtin_ia32_vbroadcastsi256((v2di)a); }
//inline (v8sf) dup(v4sf a) { return __builtin_shufflevector(a, a, 0, 1, 2, 3, 0, 1, 2, 3); }

const v8si notSignBit8 = (v8si){(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF};
inline (v8sf) abs(v8sf a) { return v8sf(notSignBit8 & v8si(a)); }

#if __GNUC__
inline (v8si) gather(const int* base, v8si indices, v8si mask) { return __builtin_ia32_gathersiv8si(_00000000, base, indices, mask, 4); }
#else
inline (v8si) gather(const int* base, v8si indices, v8si mask) { return __builtin_ia32_gatherd_d256(_00000000, (const v8si*)base, indices, mask, 4); }
#endif

#if __GNUC__
#define shuffle8(v1, v2, i0, i1, i2, i3, i4, i5, i6, i7) __builtin_shuffle(v1,v2, (v8si){i0,i1,i2,i3, 8+i4, 8+i5, 8+i6, 8+i7})
#else
#define shuffle8(v1, v2, i0, i1, i2, i3, i4, i5, i6, i7) __builtin_shufflevector(v1,v2, i0,i1,i2,i3, 8+i4, 8+i5, 8+i6, 8+i7)
#endif

inline (v4sf) low(v8sf a) { return __builtin_ia32_vextractf128_ps256(a, 0); }
inline (v4sf) high(v8sf a) { return __builtin_ia32_vextractf128_ps256(a, 1); }
//v4sf low(v8sf a) { return __builtin_shufflevector(a, a, 0, 1, 2, 3); }
//v4sf high(v8sf a) { return __builtin_shufflevector(a, a, 4, 5, 6, 7); }
inline (v8si) cvttps2dq(v8sf a) { return __builtin_ia32_cvttps2dq256(a); } // Truncates
inline (v8sf) cvtdq2ps(v8si a) { return __builtin_ia32_cvtdq2ps256(a); }
inline (float) dot8(v8sf a,  v8sf b) {
     v8sf dot4 = __builtin_ia32_dpps256(a, b, 0xFF);
      return (low(dot4)+high(dot4))[0];
 }
