#include "side.h"
typedef __SIZE_TYPE__ size_t;
typedef uint16 __attribute((__vector_size__ (8))) v4hi;
typedef int __attribute((__vector_size__(16))) v4si;
inline v4si cvttps2dq(v4sf a) { return __builtin_ia32_cvttps2dq(a); }
inline v4sf dot2(v4sf a, v4sf b) { return __builtin_ia32_dpps(a,b,0b00111111); }

// AVX
typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static constexpr v8sf _0f = float8(0);
#if __clang__
#define shuffle8(A,B, c0, c1, c2, c3, c4, c5, c6, c7) __builtin_shufflevector(A,B, c0, c1, c2, c3, c4, c5, c6, c7)
#else
#define shuffle8(A,B, c0, c1, c2, c3, c4, c5, c6, c7) (v8sf){A[c0],A[c1],A[c2],A[c3],B[c4],B[c5],B[c6],B[c7]}
#endif
#undef packed
#include <immintrin.h>
#define packed __attribute((packed))
#if 0
// QtCreator
#include <avxintrin.h>
static inline float reduce8(v8sf x) {
 const v4sf x128 = __builtin_ia32_vextractf128_ps256(x, 1) + _mm256_castps256_ps128(x);
 const __m128 x64 = x128 + _mm_movehl_ps/*__builtin_ia32_movhlps*/(x128, x128);
 const __m128 x32 = x64 + _mm_shuffle_ps/*__builtin_ia32_shufps*/(x64, x64, 0x55);
 return x32[0];
}
typedef int v8si __attribute((__vector_size__ (32)));
//static constexpr v8si _11111100 = {~0,~0,~0,~0,~0,~0,0,0};
//static inline float reduce6(v8sf x) { return reduce8((v8sf)((v8si)x & _11111100)); }
#endif
inline v8sf sqrt8(v8sf x) { return _mm256_sqrt_ps(x); }
#define EXP 0
#if EXP
#include "avx_mathfun.h"
static constexpr v8sf _11111111f = {1.f,1.f,1.f,1.f,1.f,1.f,1.f};
#endif

void side(int W /*=stride =width+2*/, const v4sf* position, v4sf* force,
          float pressure, float internodeLength, float tensionStiffness, //float radius,
          int start, int size) {
 int dy[6] {-W,            0,       W,         W,     0, -W};
 int dx[2][6] {{-1, -1, -1, 0, 1, 0},{0, -1, 0, 1, 1, 1}};
 int D[2][6];
 for(int i=0; i<2; i++) for(int e=0; e<6; e++) D[i][e] = dy[e]+dx[i][e];
 v8sf P = float8(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
 v8sf internodeLength8 = float8(internodeLength);
#if EXP
 v8sf tensionStiffness_internodeLength8 = float8(tensionStiffness*internodeLength);
#else
 v8sf tensionStiffness8 = float8(tensionStiffness);
#endif
 //float sqRadius = radius*radius;

 for(int i=start; i<start+size; i++) {
  int base = i*W;
  //assert_((W-2)%8 == 0)
  for(int j=1; j<=W-2; j+=8) {
   // Load
   int index = base+j;
   v8sf Ox, Oy, Oz;
   for(int k=0; k<8; k++) { // FIXME: packed X,Y,Z
    Ox[k] = position[index+k][0];
    Oy[k] = position[index+k][1];
    Oz[k] = position[index+k][2];
   }
   v8sf X[6], Y[6], Z[6];
   for(int e=0; e<6; e++) { // TODO: assert unrolled
    int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
    v8sf x,y,z;
    // FIXME: packed X,Y,Z
    for(int k=0; k<8; k++) x[k] = position[a+k][0];
    for(int k=0; k<8; k++) y[k] = position[a+k][1];
    for(int k=0; k<8; k++) z[k] = position[a+k][2];
    x -= Ox;
    y -= Oy;
    z -= Oz;
    X[e] = x;
    Y[e] = y;
    Z[e] = z;
   }

   // Tension
   for(int e=2; e<=4; e++) { // TODO: assert unrolled
    int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
    v8sf x = X[e], y = Y[e], z = Z[e];
    v8sf sqLength = x*x+y*y+z*z;
    v8sf length = sqrt8(sqLength);
    v8sf delta = length - internodeLength8;
#if EXP
    v8sf T = (tensionStiffness_internodeLength8 * (exp256_ps(delta/internodeLength8)-_11111111f))
      /  length;
#else
    v8sf T = (tensionStiffness8 * delta) / length;
#endif
    v8sf Fx = T * x;
    v8sf Fy = T * y;
    v8sf Fz = T * z;
    // FIXME: packed X,Y,Z
    for(int k=0; k<8; k++) force[index+k][0] += Fx[k];
    for(int k=0; k<8; k++) force[a+k][0] -= Fx[k];
    for(int k=0; k<8; k++) force[index+k][1] += Fy[k];
    for(int k=0; k<8; k++) force[a+k][1] -= Fy[k];
    for(int k=0; k<8; k++) force[index+k][2] += Fz[k];
    for(int k=0; k<8; k++) force[a+k][2] -= Fz[k];
   }

   // Pressure
   v8sf Px = _0f, Py = _0f, Pz = _0f;
   for(int a=0; a<6; a++) { // TODO: assert peeled
    int b = (a+1)%6;
    Px += (Y[a]*Z[b] - Y[b]*Z[a]);
    Py += (Z[a]*X[b] - Z[b]*X[a]);
    Pz += (X[a]*Y[b] - X[b]*Y[a]);
   }
   Px *= P;
   Py *= P;
   Pz *= P;
   // FIXME: packed X,Y,Z
   for(int k=0; k<8; k++) force[index+k][0] += Px[k];
   for(int k=0; k<8; k++) force[index+k][1] += Py[k];
   for(int k=0; k<8; k++) force[index+k][2] += Pz[k];
  }
 }
}
