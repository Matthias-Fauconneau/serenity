#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"
//#include "immintrin.h"

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

typedef int v8si __attribute((__vector_size__ (32)));
typedef double v2df __attribute((__vector_size__ (16)));

// v4sf
typedef float v4sf __attribute((__vector_size__ (16)));
inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }
unused const v4sf _1f = float4( 1 );
unused const v4sf _0f = float4( 0 );

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline v4sf loadu(const float* const ptr) { return (v4sf)__builtin_ia32_lddqu((byte*)ptr); }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline v4sf bitOr(v4sf a, v4sf b) { return v4si(a) | v4si(b); } //__builtin_ia32_orps(a, b); }

const v4si notSignBit = (v4si){(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF,(int)0x7FFFFFFF};
inline v4sf abs(v4sf a) { return notSignBit & (v4si)a; }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
#define shuffle(v1, v2, i1, i2, i3, i4) __builtin_shufflevector(v1,v2, i1,i2, 4+(i3), 4+(i4))
inline v4sf hadd(v4sf a, v4sf b) { return __builtin_ia32_haddps(a,b); } //a0+a1, a2+a3, b0+b1, b2+b3
inline v4sf dot4(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0xFF); }
inline v4sf hsum(v4sf a) { return dot4(a,_1f); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }
#define blendps __builtin_ia32_blendps
inline v4sf blendv(v4sf a, v4sf b, v4sf m) { return __builtin_ia32_blendvps(a, b, m); }
inline v4sf dot2(v4sf a, v4sf b) { v4sf sq = a*b; return hadd(sq,sq); }
inline v4sf hmin(v4sf a) { a = min(a, shuffle(a, a, 1,0,3,2)); return min(a, shuffle(a, a, 2,2,0,0)); }
inline v4sf hmax(v4sf a) { a = max(a, shuffle(a, a, 1,0,3,2)); return max(a, shuffle(a, a, 2,2,0,0)); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); } // Rounds
inline v4si cvttps2dq(v4sf a) { return __builtin_ia32_cvttps2dq(a); } // Truncates
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }
// v4df

typedef double v4df __attribute((__vector_size__ (32)));
inline v4df constexpr double4(double f) { return (v4df){f,f,f,f}; }
unused const v4df _1d = double4( 1 );
unused const v4df _0d = double4( 0 );

inline v4df loada(const double* const ptr) { return *(v4df*)ptr; }
//inline v4df loadu(const double* const ptr) { return (v4df)_mm256_load_pd((byte*)ptr); }
inline void storea(double* const ptr, v4df a) { *(v4df*)ptr = a; }

inline v4df bitOr(v4df a, v4df b) { return v8si(a) | v8si(b); } //__builtin_ia32_orps(a, b); }

const v8si notSignBitd = (v8si){(int)0x7FFFFFFF,(int)0xFFFFFFFF,(int)0x7FFFFFFF,(int)0xFFFFFFFF,(int)0x7FFFFFFF,(int)0xFFFFFFFF,(int)0x7FFFFFFF,(int)0xFFFFFFFF};
inline v4df abs(v4df a) { return notSignBitd & (v8si)a; }

inline v4df min(v4df a, v4df b) { return __builtin_ia32_minpd256(a,b); }
//inline v4df max(v4df a, v4df b) { return __builtin_ia32_maxpd256(a,b); }
inline v4df max(v4df a, v4df b) { return {max(a[0],b[0]), max(a[1],b[1]), max(a[2],b[2]), max(a[3],b[3])}; }
#define shuffle(v1, v2, i1, i2, i3, i4) __builtin_shufflevector(v1,v2, i1,i2, 4+(i3), 4+(i4))
//inline v4df hadd(v4df a, v4df b) { return __builtin_ia32_haddpd256(a,b); } //a0+a1, a2+a3, b0+b1, b2+b3
inline v4df hadd(v4df a, v4df b) { return {a[0]+a[1], a[2]+a[3], b[0]+b[1], b[2]+b[3] }; } //a0+a1, a2+a3, b0+b1, b2+b3
inline v2df dot4(v4df a, v4df b) {
    v4df xy = a * b; v4df t = hadd( xy, xy );
    return __builtin_ia32_vextractf128_pd256(t, 0) + __builtin_ia32_vextractf128_pd256(t, 1);
}
inline v2df hsum(v4df a) { return dot4(a,_1d); }
inline v4df rcp(v4df a) { return {1./a[0], 1./a[1], 1./a[2], 1./a[3]}; }
inline v4df sqrt(v4df a) { return {sqrt(a[0]), sqrt(a[1]), sqrt(a[2]), sqrt(a[3])}; }

//inline int mask(v4df a) { return __builtin_ia32_movmskpd256(a); }
inline int mask(v4df a) { return (a[0]?1:0)|(a[1]?2:0)|(a[2]?4:0)|(a[3]?8:0); }
#define blendpd __builtin_ia32_blendpd256
//inline v4df blendv(v4df a, v4df b, v4df m) { return __builtin_ia32_blendvpd256(a, b, m); }
inline v4df blendv(v4df a, v4df b, v4df m) { return {m[0]?b[0]:a[0], m[1]?b[1]:a[1], m[2]?b[2]:a[2], m[3]?b[3]:a[3]}; }
inline v4df dot2(v4df a, v4df b) { v4df sq = a*b; return hadd(sq,sq); }
//inline v4df hmin(v4df a) { a = min(a, shuffle(a, a, 1,0,3,2)); return min(a, shuffle(a, a, 2,2,0,0)); }
inline v4df hmin(v4df a) { return double4(min(min(min(a[0],a[1]),a[2]),a[3])); }
//inline v4df hmax(v4df a) { a = max(a, shuffle(a, a, 1,0,3,2)); return max(a, shuffle(a, a, 2,2,0,0)); }
inline v4df hmax(v4df a) { return double4(max(max(max(a[0],a[1]),a[2]),a[3])); }

//inline v4si cvtpd2dq(v4df a) { return __builtin_ia32_cvtpd2dq256(a); } // Rounds
//inline v4si cvttpd2dq(v4df a) { return __builtin_ia32_cvttpd2dq256(a); } // Truncates
inline v4si cvttpd2dq(v4df a) { return v4si{int(a[0]),int(a[1]),int(a[2]),int(a[3])}; } // Truncates
//inline v4df cvtdq2pd(v4si a) { return __builtin_ia32_cvtdq2pd256(a); }
inline v4df cvtdq2pd(v4si a) { return v4df{double(a[0]),double(a[1]),double(a[2]),double(a[3])}; }
