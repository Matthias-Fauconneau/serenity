#include "simulation.h"
#include "parallel.h"

void Simulation::stepMembrane() {
 /*membraneInitializationTime +=*/ //parallel_chunk(align(simd, membrane.count)/simd, [&](uint, size_t start, size_t size) {
 {
  size_t start = membrane.stride/simd, size = (membrane.count-membrane.stride)/simd;
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   store(membrane.Fx, i, _0f);
   store(membrane.Fy, i, _0f);
   store(membrane.Fz, i, _0f);
  }
 }//, 1);

 //parallel_chunk(1, membrane.H-1, [this](uint, size_t start, size_t size) {
 size_t start = 1, size = membrane.H-1;
 ref<float> Px = membrane.Px.slice(simd);
 ref<float> Py = membrane.Py.slice(simd);
 ref<float> Pz = membrane.Pz.slice(simd);
 mref<float> Fx = membrane.Fx.slice(simd);
 mref<float> Fy = membrane.Fy.slice(simd);
 mref<float> Fz = membrane.Fz.slice(simd);

 int D[2][6];
 int dy[6] {-1, -1,            0,       1,         1,     0};
 int dx[2][6] {{0, -1, -1, -1, 0, 1},{1, 0, -1, 0, 1, 1}};
 int stride = membrane.stride, W = membrane.W;
 for(int i=0; i<2; i++) for(int e=0; e<6; e++) D[i][e] = dy[e]*stride+dx[i][e];
 v8sf P = float8(/*alpha**/pressure/(2*3)); // area = length(cross)/2 / 3 vertices
 v8sf internodeLength8 = float8(membrane.internodeLength);
 v8sf tensionStiffness_internodeLength8 = float8(membrane.tensionStiffness*membrane.internodeLength);

 /*// Tension from previous row
  {
   size_t i = start;
   int base = i*membrane.stride;
   for(int j=1; j<=W; j+=8) {
    // Load
    int index = base+j;
    v8sf Ox = *(v8sf*)(Px+index);
    v8sf Oy = *(v8sf*)(Py+index);
    v8sf Oz = *(v8sf*)(Pz+index);

    // Tension
    v8sf fx = _0f, fy = _0f, fz = _0f; // Assumes accumulators stays in registers
    for(int a=0; a<2; a++) { // TODO: assert unrolled
     int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
     v8sf x = loadu(Px+e) - Ox;
     v8sf y = loadu(Py+e) - Oy;
     v8sf z = loadu(Pz+e) - Oz;
     v8sf sqLength = x*x+y*y+z*z;
     v8sf length = sqrt8(sqLength);
     v8sf delta = length - internodeLength8;
     v8sf u = delta/internodeLength8;
#define U u + u*u + u*u*u
     //#define U u + u*u
     v8sf T = (tensionStiffness_internodeLength8 * (U)) /  length;
     v8sf tx = T * x;
     v8sf ty = T * y;
     v8sf tz = T * z;
     fx += tx;
     fy += ty;
     fz += tz;
    }
    *(v8sf*)(Fx+index) += fx;
    *(v8sf*)(Fy+index) += fy;
    *(v8sf*)(Fz+index) += fz;
    for(size_t k: range(8)) forces.append(membrane.position(7+index+k), -vec3(fx[k], fy[k], fz[k]));
   }
  }*/

 for(size_t i = start/*+1*/; i < start+size; i++) {
  int base = i*membrane.stride;
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
    v8sf sqLength = x*x+y*y+z*z;
    v8sf length = sqrt(sqLength);
    v8sf delta = length - internodeLength8;
    v8sf u = delta/internodeLength8;
#define U u + u*u // + u*u*u
    v8sf T = (tensionStiffness_internodeLength8 * (U)) /  length;
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
   for(size_t a: range(2)) {
    int b = a+1; // TODO: Assert peeled
    v8sf px = (Y[a]*Z[b] - Y[b]*Z[a]);
    v8sf py = (Z[a]*X[b] - Z[b]*X[a]);
    v8sf pz = (X[a]*Y[b] - X[b]*Y[a]);
    /*v8sf dot2 = Ox*px + Oy*py;
       v8sf p = (v8sf)((v8si)P & (v8si)(dot2 < _0f)); // Assumes an extra multiplication is more efficient than an extra accumulator*/
    v8sf p = P;
    v8sf ppx = p * px;
    v8sf ppy = p * py;
    v8sf ppz = p * pz;
    FX[a] += ppx;
    FY[a] += ppy;
    FZ[a] += ppz;
    FX[b] += ppx;
    FY[b] += ppy;
    FZ[b] += ppz;
    FX[3] += ppx;
    FY[3] += ppy;
    FZ[3] += ppz;
   }
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

 /*{ // Tension from next row
   size_t i = start+size;
   int base = i*membrane.stride;
   *(v8sf*)(Fx+1) = _0f;
   *(v8sf*)(Fy+1) = _0f;
   *(v8sf*)(Fz+1) = _0f;
   for(int j=1; j<=W; j+=8) {
    *(v8sf*)(Fx+j+8) = _0f;
    *(v8sf*)(Fy+j+8) = _0f;
    *(v8sf*)(Fz+j+8) = _0f;
    // Load
    int index = base+j;
    v8sf Ox = *(v8sf*)(Px+index);
    v8sf Oy = *(v8sf*)(Py+index);
    v8sf Oz = *(v8sf*)(Pz+index);

    // Tension
    for(int a=0; a<2; a++) { // TODO: assert unrolled
     int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
     v8sf x = loadu(Px+e) - Ox;
     v8sf y = loadu(Py+e) - Oy;
     v8sf z = loadu(Pz+e) - Oz;
     v8sf sqLength = x*x+y*y+z*z;
     v8sf length = sqrt8(sqLength);
     v8sf delta = length - internodeLength8;
     v8sf u = delta/internodeLength8;
     v8sf T = (tensionStiffness_internodeLength8 * (U)) /  length;
     v8sf tx = T * x;
     v8sf ty = T * y;
     v8sf tz = T * z;
     store(Fx+e, loadu(Fx+e) - tx);
     store(Fy+e, loadu(Fy+e) - ty);
     store(Fz+e, loadu(Fz+e) - tz);
    }
   }
  }*/
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 float maxMembraneV_[maxThreadCount] = {};
 //membraneIntegrationTime +=
 //parallel_chunk(align(simd, membrane.count)/simd, [this,&maxMembraneV_](uint id, size_t start, size_t size) {
 {size_t id = 0;
   const v8sf dt_mass = float8(dt / membrane.mass), dt = float8(this->dt);
   v8sf maxMembraneV8 = _0f;
   float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
   float* const pVx = membrane.Vx.begin(), *pVy = membrane.Vy.begin(), *pVz = membrane.Vz.begin();
   float* const Px = membrane.Px.begin(), *Py = membrane.Py.begin(), *Pz = membrane.Pz.begin();
   for(size_t i=1; i<membrane.H; i+=1) {
    // Adds force from repeated nodes
    Fx[i*membrane.stride+simd+0] += Fx[simd+i*membrane.stride+membrane.W];
    Fy[i*membrane.stride+simd+0] += Fy[simd+i*membrane.stride+membrane.W];
    Fz[i*membrane.stride+simd+0] += Fz[simd+i*membrane.stride+membrane.W];
    Fx[simd+i*membrane.stride+membrane.W-1] += Fx[i*membrane.stride+simd-1];
    Fy[simd+i*membrane.stride+membrane.W-1] += Fy[i*membrane.stride+simd-1];
    Fz[simd+i*membrane.stride+membrane.W-1] += Fz[i*membrane.stride+simd-1];
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
   maxMembraneV_[id] = maxMembraneV;
 }//, 1);
 float maxMembraneV = 0;
 for(size_t k: range(threadCount)) maxMembraneV = ::max(maxMembraneV, maxMembraneV_[k]);
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}
