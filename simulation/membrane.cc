#include "simulation.h"
#include "parallel.h"

void Simulation::stepMembrane() {
 for(size_t i=membrane.stride; i<membrane.count-membrane.stride; i+=simd) {
  store(membrane.Fx, i, _0f);
  store(membrane.Fy, i, _0f);
  store(membrane.Fz, i, _0f);
 }

 ref<float> Px = membrane.Px, Py = membrane.Py, Pz = membrane.Pz;
 mref<float> Fx = membrane.Fx, Fy = membrane.Fy, Fz = membrane.Fz;

 int D[2][6];
 int dy[6] {-1, -1,            0,       1,         1,     0};
 int dx[2][6] {{0, -1, -1, -1, 0, 1},{1, 0, -1, 0, 1, 1}};
 int stride = membrane.stride, W = membrane.W;
 for(int i=0; i<2; i++) for(int e=0; e<6; e++) D[i][e] = dy[e]*stride+dx[i][e];
 //v8sf P = float8(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
 v8sf internodeLength = float8(membrane.internodeLength);
 v8sf tensionStiffness_internodeLength = float8(membrane.tensionStiffness*membrane.internodeLength);
 log(timeStep);
 for(size_t i=1; i<membrane.H-1; i++) {
  int base = i*membrane.stride+simd;
  for(int j=0; j<W; j+=simd) {
   int index = base+j;
   v8sf Ox = load(Px, index);
   v8sf Oy = load(Py, index);
   v8sf Oz = load(Pz, index);
   int E[3];
   v8sf X[3], Y[3], Z[3];
   v8sf FX[4], FY[4], FZ[4];
   for(size_t a: range(4)) { FX[a]=_0f, FY[a]=_0f, FZ[a]=_0f; } // TODO: assert unrolled
   // Tension
   for(size_t a: range(3)) { // TODO: assert unrolled
    int e = E[a] = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
    v8sf x = X[a] = loadu(Px, e) - Ox;
    v8sf y = Y[a] = loadu(Py, e) - Oy;
    v8sf z = Z[a] = loadu(Pz, e) - Oz;
    for(size_t k: range(simd)) assert(isNumber(x[k]));
    for(size_t k: range(simd)) assert(isNumber(Ox[k]));
    for(size_t k: range(simd)) assert(isNumber(y[k]));
    for(size_t k: range(simd)) assert(isNumber(Oy[k]));
    for(size_t k: range(simd)) assert(isNumber(z[k]));
    for(size_t k: range(simd)) assert(isNumber(Oz[k]));
    v8sf length = sqrt(x*x+y*y+z*z);
    for(size_t k: range(simd)) assert(isNumber(length[k]), x[k], y[k], z[k], (x*x+y*y+z*z)[k],
                                      loadu(Pz, e)[k], Oz[k], length[k]);
    for(size_t k: range(simd)) assert(length[k]);
    v8sf delta = length - internodeLength;
    v8sf u = delta / internodeLength;
#define U u //+ u*u // + u*u*u
    v8sf T = (tensionStiffness_internodeLength * (U)) /  length;
    v8sf tx = T * x;
    v8sf ty = T * y;
    v8sf tz = T * z;
    FX[a] -= tx;
    FY[a] -= ty;
    FZ[a] -= tz;
    FX[3] += tx;
    FY[3] += ty;
    FZ[3] += tz;
   }
   /*for(size_t a: range(2)) {
    int b = a+1; // TODO: Assert peeled
    v8sf px = (Y[a]*Z[b] - Y[b]*Z[a]);
    v8sf py = (Z[a]*X[b] - Z[b]*X[a]);
    v8sf pz = (X[a]*Y[b] - X[b]*Y[a]);
    v8sf ppx = P * px;
    v8sf ppy = P * py;
    v8sf ppz = P * pz;
    FX[a] += ppx;
    FY[a] += ppy;
    FZ[a] += ppz;
    FX[b] += ppx;
    FY[b] += ppy;
    FZ[b] += ppz;
    FX[3] += ppx;
    FY[3] += ppy;
    FZ[3] += ppz;
   }*/
   for(int a=0; a<3; a++) {
    int e = E[a];
    storeu(Fx, e, loadu(Fx, e) + FX[a]);
    storeu(Fy, e, loadu(Fy, e) + FY[a]);
    storeu(Fz, e, loadu(Fz, e) + FZ[a]);
   }
   store(Fx, index, FX[3]);
   store(Fy, index, FY[3]);
   store(Fz, index, FZ[3]);
  }
 }
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 const v8sf dt_mass = float8(this->dt / membrane.mass), dt = float8(this->dt);
 v8sf maxMembraneV8 = _0f;
 float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
 float* const pVx = membrane.Vx.begin(), *pVy = membrane.Vy.begin(), *pVz = membrane.Vz.begin();
 float* const Px = membrane.Px.begin(), *Py = membrane.Py.begin(), *Pz = membrane.Pz.begin();
 for(size_t i=1; i<membrane.H-1; i+=1) {
  // Adds force from repeated nodes
  Fx[i*membrane.stride+simd+0] += Fx[i*membrane.stride+simd+membrane.W];
  Fy[i*membrane.stride+simd+0] += Fy[i*membrane.stride+simd+membrane.W];
  Fz[i*membrane.stride+simd+0] += Fz[i*membrane.stride+simd+membrane.W];
  Fx[i*membrane.stride+simd+membrane.W-1] += Fx[i*membrane.stride+simd-1];
  Fy[i*membrane.stride+simd+membrane.W-1] += Fy[i*membrane.stride+simd-1];
  Fz[i*membrane.stride+simd+membrane.W-1] += Fz[i*membrane.stride+simd-1];
  for(size_t j=0; j<membrane.W; j+=simd) {
   size_t k = i*membrane.stride+simd+j;
   // Symplectic Euler
   v8sf Vx = load(pVx, k), Vy = load(pVy, k), Vz = load(pVz, k);
   Vx += dt_mass * load(Fx, k);
   Vy += dt_mass * load(Fy, k);
   Vz += dt_mass * load(Fz, k);
   store(pVx, k, Vx);
   store(pVy, k, Vy);
   store(pVz, k, Vz);
   store(Px, k, load(Px, k) + dt * Vx);
   store(Py, k, load(Py, k) + dt * Vy);
   store(Pz, k, load(Pz, k) + dt * Vz);
   maxMembraneV8 = max(maxMembraneV8, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
  }
 }
 float maxMembraneV = 0;
 for(size_t k: range(simd)) maxMembraneV = ::max(maxMembraneV, maxMembraneV8[k]);
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}
