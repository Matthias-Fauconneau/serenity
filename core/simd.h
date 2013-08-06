#pragma once
/// \file simd.h SIMD intrinsics (SSE, AVX, ...)
#include "core.h"

typedef long long v2di __attribute__ ((__vector_size__ (16)));
typedef int v4si __attribute__ ((__vector_size__ (16)));
typedef short v8hi __attribute__ ((__vector_size__ (16)));
typedef char v16qi __attribute__ ((__vector_size__ (16)));
typedef float v4sf __attribute__ ((__vector_size__ (16)));

inline void movnt(uint64* const ptr, uint64 v) { return  __builtin_ia32_movntq(ptr,v); }
inline void sfence() { __builtin_ia32_sfence(); }

// v2di

inline void storeu(long long* const ptr, v2di a) { __builtin_ia32_storedqu((char*)ptr, (v16qi)a); }

// v4si

inline v4si set1(int i) { return (v4si){i,i,i,i}; }
inline v4si loada(const uint32* const ptr) { return *(v4si*)ptr; }
inline v4si loadu(const uint32* const ptr) { return (v4si)__builtin_ia32_loaddqu((char*)ptr); }
inline void storea(uint32* const ptr, v4si a) { *(v4si*)ptr = a; }
inline void storeu(uint32* const ptr, v4si a) { __builtin_ia32_storedqu((char*)ptr, (v16qi)a); }
inline v4si max(v4si a, v4si b) { return __builtin_ia32_pmaxud128(a,b); }
//inline v4si cmpgt(v4si a, v4si b) { return __builtin_ia32_pcmpgtd128(a, b); }
inline v8hi packus(v4si a, v4si b) { return __builtin_ia32_packusdw128(a,b); }
//inline v4si blendv(v4si a, v4si b, v4si m) { return (v4si)__builtin_ia32_blendvps((v4sf)a, (v4sf)b, (v4sf)m); }

#if NO_INLINE
#define extracti __builtin_ia32_vec_ext_v4si
#else
inline int extracti(v4si a, int index) { return __builtin_ia32_vec_ext_v4si(a, index); }
#endif

// v8hi

inline v8hi short8(int16 i) { return (v8hi){i,i,i,i,i,i,i,i}; }
inline v8hi loada(const uint16* const ptr) { return *(v8hi*)ptr; }
inline v8hi loadu(const uint16* const ptr) { return (v8hi)__builtin_ia32_loaddqu((char*)ptr); }
inline void storea(uint16* const ptr, v8hi a) { *(v8hi*)ptr = a; }
inline void storeu(uint16* const ptr, v8hi a) { __builtin_ia32_storedqu((char*)ptr, (v16qi)a); }

inline v8hi shiftRight(v8hi a, uint imm) { return __builtin_ia32_psrlwi128(a, imm); }

inline v8hi min(v8hi a, v8hi b) { return __builtin_ia32_pminuw128(a,b); }
inline v8hi max(v8hi a, v8hi b) { return __builtin_ia32_pmaxuw128(a,b); }

//inline v8hi cmpgt(v8hi a, v8hi b) { return __builtin_ia32_pcmpgtw128(a, b); }

inline v4si unpacklo(v8hi a, v8hi b) { return (v4si)__builtin_ia32_punpcklwd128(a, b); }
inline v4si unpackhi(v8hi a, v8hi b) { return (v4si)__builtin_ia32_punpckhwd128(a, b); }
inline v2di unpacklo(v4si a, v4si b) { return (v2di)__builtin_ia32_punpckldq128(a, b); }
inline v2di unpackhi(v4si a, v4si b) { return (v2di)__builtin_ia32_punpckhdq128(a, b); }
inline v8hi unpacklo(v2di a, v2di b) { return (v8hi)__builtin_ia32_punpcklqdq128(a, b); }
inline v8hi unpackhi(v2di a, v2di b) { return (v8hi)__builtin_ia32_punpckhqdq128(a, b); }

/// Transposes 8 registers of 8 16bit values
inline void transpose8(uint16* out, uint stride, v8hi a, v8hi b, v8hi c, v8hi d, v8hi e, v8hi f, v8hi g, v8hi h) {
    v4si a03b03 = unpacklo(a, b);
    v4si a47b47 = unpackhi(a, b);
    v4si c03d03 = unpacklo(c, d);
    v4si c47d47 = unpackhi(c, d);
    v4si e03f03 = unpacklo(e, f);
    v4si e47f47 = unpackhi(e, f);
    v4si g03h03 = unpacklo(g, h);
    v4si g47h47 = unpackhi(g, h);

    v2di a01b01c01d01 = unpacklo(a03b03, c03d03);
    v2di a23b23c23d23 = unpackhi(a03b03, c03d03);
    v2di e01f01g01h01 = unpacklo(e03f03, g03h03);
    v2di e23f23g23h23 = unpackhi(e03f03, g03h03);
    v2di a45b45c45d45 = unpacklo(a47b47, c47d47);
    v2di a67b67c67d67 = unpackhi(a47b47, c47d47);
    v2di e45f45g45h45 = unpacklo(e47f47, g47h47);
    v2di e67f67g67h67 = unpackhi(e47f47, g47h47);

    storeu(out+0*stride, unpacklo(a01b01c01d01, e01f01g01h01));
    storeu(out+1*stride, unpackhi(a01b01c01d01, e01f01g01h01));
    storeu(out+2*stride, unpacklo(a23b23c23d23, e23f23g23h23));
    storeu(out+3*stride, unpackhi(a23b23c23d23, e23f23g23h23));
    storeu(out+4*stride, unpacklo(a45b45c45d45, e45f45g45h45));
    storeu(out+5*stride, unpackhi(a45b45c45d45, e45f45g45h45));
    storeu(out+6*stride, unpacklo(a67b67c67d67, e67f67g67h67));
    storeu(out+7*stride, unpackhi(a67b67c67d67, e67f67g67h67));
}

// v16qi

inline v16qi set1(char i) { return (v16qi){i,i,i,i,i,i,i,i,i,i,i,i,i,i,i,i}; }
inline v16qi packus(v8hi a, v8hi b) { return __builtin_ia32_packuswb128(a,b); }

// v4sf

inline v4sf constexpr float4(float f) { return (v4sf){f,f,f,f}; }

inline v4sf loada(const float* const ptr) { return *(v4sf*)ptr; }
inline void storea(float* const ptr, v4sf a) { *(v4sf*)ptr = a; }

inline v4sf bitOr(v4sf a, v4sf b) { return __builtin_ia32_orps(a, b); }
inline v4sf andnot(v4sf a, v4sf b) { return __builtin_ia32_andnps(a, b); }
inline v4sf bitXor(v4sf a, v4sf b) { return __builtin_ia32_xorps(a, b); }
//inline v4sf cmpgt(v4sf a, v4sf b) { return __builtin_ia32_cmpgtps(a, b); }

const v4sf signBit = (v4sf)(v4si){(int)0x80000000,(int)0x80000000,(int)0x80000000,(int)0x80000000};
inline v4sf negate(v4sf a) { return bitXor(a,  signBit); }
inline v4sf abs(v4sf a) { return andnot(signBit, a); }

inline v4sf min(v4sf a, v4sf b) { return __builtin_ia32_minps(a,b); }
inline v4sf max(v4sf a, v4sf b) { return __builtin_ia32_maxps(a,b); }
#if NO_INLINE
#define shuffle( a,  b,  x,  y,  z,  w) __builtin_ia32_shufps(a, b, (w)<<6|(z)<<4|(y)<<2|(x))
#else
inline v4sf shuffle(v4sf a, v4sf b, int x, int y, int z, int w) { return __builtin_ia32_shufps(a, b, w<<6|z<<4|y<<2|x); }
#endif
inline v4sf hadd(v4sf a, v4sf b) { return __builtin_ia32_haddps(a,b); } //a0+a1, a2+a3, b0+b1, b2+b3
inline v4sf dot3(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0x7f); }
inline v4sf dot4(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0xFF); }
inline v4sf rcp(v4sf a) { return __builtin_ia32_rcpps(a); }
inline v4sf rsqrt(v4sf a) { return __builtin_ia32_rsqrtps(a); }
inline v4sf sqrt(v4sf a) { return __builtin_ia32_sqrtps(a); }

inline int mask(v4sf a) { return __builtin_ia32_movmskps(a); }
#if NO_INLINE
#define blend( a, b, m) __builtin_ia32_blendps(a, b, m)
#else
inline v4sf blend(v4sf a, v4sf b, int m) { return __builtin_ia32_blendps(a, b, m); }
#endif
inline v4sf blendv(v4sf a, v4sf b, v4sf m) { return __builtin_ia32_blendvps(a, b, m); }
#if NO_INLINE
#define extractf __builtin_ia32_vec_ext_v4sf
#else
inline float extractf(v4sf a, int index) { return __builtin_ia32_vec_ext_v4sf(a, index); }
#endif

inline v4sf transpose(v4sf a, v4sf b, v4sf c, v4sf d) { return shuffle(shuffle(a,b,0,0,0,0), shuffle(c,d,0,0,0,0),0,2,0,2); }
inline v4sf dot2(v4sf a, v4sf b) { v4sf sq = a*b; return hadd(sq,sq); }
inline v4sf hmin(v4sf a) { a = min(a, shuffle(a, a, 1,0,3,2)); return min(a, shuffle(a, a, 2,2,0,0)); }
inline v4sf hmax(v4sf a) { a = max(a, shuffle(a, a, 1,0,3,2)); return max(a, shuffle(a, a, 2,2,0,0)); }
inline v4sf normalize3(v4sf a) { return a * rsqrt(dot3(a,a)); }

inline v4si cvtps2dq(v4sf a) { return __builtin_ia32_cvtps2dq(a); }
inline v4sf cvtdq2ps(v4si a) { return __builtin_ia32_cvtdq2ps(a); }

//inline v8si cvtps2dq(v8sf a) { return __builtin_ia32_cvtps2dq256(a); }
//typedef long long m128i __attribute__ ((__vector_size__ (16), __may_alias__));
//inline v8hi packus(v8si a) { return __builtin_ia32_packusdw128(__builtin_ia32_vextractf128_si256(a,0),__builtin_ia32_vextractf128_si256(a,1)); }

// Constants
unused const v4si _1i = {1,1,1,1};
unused const v8hi _0h = {0,0,0,0};

unused const v4sf _1f = float4( 1 );
unused const v4sf _0f = float4( 0 );
unused const v4sf _halff = float4( 1./2 );
unused const v4sf _2f = float4( 2 );
unused const v4sf _4f = float4( 4 );
unused const v4sf _0001f = {0, 0, 0, 1};
unused const v4sf _0101f = {0, 1, 0, 1};
unused const v4sf _1110f = {1, 1, 1, 0};
#define FLT_MAX __FLT_MAX__
unused const v4sf mfloatMax = float4(-FLT_MAX);
unused const v4sf floatMax = float4(FLT_MAX);
unused const v4sf floatMMMm = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX};
unused const v4sf alphaTerm = {FLT_MAX, FLT_MAX, FLT_MAX, /*0.95*/1};
unused const v4sf scaleTo8bit = float4(0xFF);
unused const v4sf scaleFrom8bit = float4(1./0xFF);
unused const v4sf scaleFrom16bit = float4(1./0xFFFF);
unused const v4sf signPPNN = (v4sf)(v4si){0,0,(int)0x80000000,(int)0x80000000};
