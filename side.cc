#include "side.h"

// AVX
typedef float v8sf __attribute((__vector_size__ (32)));
inline v8sf constexpr float8(float f) { return (v8sf){f,f,f,f,f,f,f,f}; }
static constexpr v8sf _0f = float8(0);
#define EXP 0
#if EXP
#include "avx_mathfun.h"
static constexpr v8sf _11111111f = {1.f,1.f,1.f,1.f,1.f,1.f,1.f};
#endif

void side(int W /*=stride =width+2*/, const float* Px, const float* Py, const float* Pz,
          const float* Fx, const float* Fy, const float* Fz,
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
   v8sf Ox = *(v8sf*)(Px+index);
   v8sf Oy = *(v8sf*)(Py+index);
   v8sf Oz = *(v8sf*)(Pz+index);

   v8sf X[6], Y[6], Z[6];
   for(int e=0; e<6; e++) { // TODO: assert unrolled
    int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
    X[e] = *(v8sf*)(Px+a) - Ox;
    Y[e] = *(v8sf*)(Py+a) - Oy;
    Z[e] = *(v8sf*)(Pz+a) - Oz;
   }

   // Tension
   for(int e=2; e<=4; e++) { // TODO: assert unrolled
    int a = index+D[i%2][e]; // Gather (TODO: assert reduced i%2)
    v8sf x = X[e], y = Y[e], z = Z[e];
    v8sf sqLength = x*x+y*y+z*z;
    v8sf length = __builtin_ia32_sqrtps256(sqLength);
    v8sf delta = length - internodeLength8;
#if EXP
    v8sf T = (tensionStiffness_internodeLength8 * (exp256_ps(delta/internodeLength8)-_11111111f))
      /  length;
#else
    v8sf T = (tensionStiffness8 * delta) / length;
#endif
    v8sf fx = T * x;
    v8sf fy = T * y;
    v8sf fz = T * z;
    *(v8sf*)(Fx+index) += fx;
    *(v8sf*)(Fy+index) += fy;
    *(v8sf*)(Fz+index) += fz;
    *(v8sf*)(Fx+a) -= fx;
    *(v8sf*)(Fy+a) -= fy;
    *(v8sf*)(Fz+a) -= fz;
   }

   // Pressure
   v8sf Px = _0f, Py = _0f, Pz = _0f;
   for(int a=0; a<6; a++) { // TODO: assert peeled
    int b = (a+1)%6;
    Px += (Y[a]*Z[b] - Y[b]*Z[a]);
    Py += (Z[a]*X[b] - Z[b]*X[a]);
    Pz += (X[a]*Y[b] - X[b]*Y[a]);
   }
   *(v8sf*)(Fx+index) += P * Px;
   *(v8sf*)(Fy+index) += P * Py;
   *(v8sf*)(Fz+index) += P * Pz;
  }
 }
}
