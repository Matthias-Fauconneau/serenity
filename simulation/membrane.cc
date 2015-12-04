#include "simulation.h"
#include "parallel.h"

void Simulation::stepMembrane() {
 membraneInitializationTime.start();
 for(size_t i=membrane.stride; i<membrane.count-membrane.stride; i+=simd) {
  store(membrane.Fx, i, _0f);
  store(membrane.Fy, i, _0f);
  store(membrane.Fz, i, _0f);
 }
 membraneInitializationTime.stop();

 membraneForceTime += parallel_chunk(1, membrane.H-1, [this](uint, size_t start, size_t size) {
   ref<float> Px = membrane.Px, Py = membrane.Py, Pz = membrane.Pz;
   ref<float> Vx = membrane.Vx, Vy = membrane.Vy, Vz = membrane.Vz;
   mref<float> Fx = membrane.Fx, Fy = membrane.Fy, Fz = membrane.Fz;

   int D[2][6];
   int dy[6] {-1, -1,            0,       1,         1,     0};
   int dx[2][6] {{0, -1, -1, -1, 0, 1},{1, 0, -1, 0, 1, 1}};
   int stride = membrane.stride, W = membrane.W;
   for(int i=0; i<2; i++) for(int e=0; e<6; e++) D[i][e] = dy[e]*stride+dx[i][e];
   v8sf P = float8(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
   v8sf internodeLength = float8(membrane.internodeLength);
   v8sf tensionStiffness = float8(membrane.tensionStiffness);

   // Tension from previous row
   {
    size_t i = start;
    int base = i*stride+simd;
    for(int j=0; j<W; j+=simd) {
     int index = base+j;
     v8sf Ox = load(Px, index);
     v8sf Oy = load(Py, index);
     v8sf Oz = load(Pz, index);
     v8sf VOx = load(Vx, index);
     v8sf VOy = load(Vy, index);
     v8sf VOz = load(Vz, index);

     // Tension
     v8sf fx = _0f, fy = _0f, fz = _0f; // Assumes accumulators stays in registers
     for(int a=0; a<2; a++) { // TODO: assert unrolled
      int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      v8sf Rx = loadu(Px, e) - Ox;
      v8sf Ry = loadu(Py, e) - Oy;
      v8sf Rz = loadu(Pz, e) - Oz;
      v8sf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      v8sf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      v8sf x = L - internodeLength;
      v8sf fS = tensionStiffness * x;
      v8sf RVx = loadu(Vx, e) - VOx;
      v8sf RVy = loadu(Vy, e) - VOy;
      v8sf RVz = loadu(Vz, e) - VOz;
      v8sf fB = float8(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      v8sf f = fS + fB;
      v8sf tx = f * Nx;
      v8sf ty = f * Ny;
      v8sf tz = f * Nz;
      fx += tx;
      fy += ty;
      fz += tz;
     }
     store(Fx, index, load(Fx, index) + fx);
     store(Fy, index, load(Fy, index) + fx);
     store(Fz, index, load(Fz, index) + fx);
    }
   }

   for(size_t i=start; i<start+size; i++) {
    int base = i*stride+simd;
    for(int j=0; j<W; j+=simd) {
     int index = base+j;
     v8sf Ox = load(Px, index);
     v8sf Oy = load(Py, index);
     v8sf Oz = load(Pz, index);
     v8sf VOx = load(Vx, index);
     v8sf VOy = load(Vy, index);
     v8sf VOz = load(Vz, index);

     int E[3];
     v8sf X[3], Y[3], Z[3];
     v8sf FX[4], FY[4], FZ[4];
     for(size_t a: range(4)) { FX[a]=_0f, FY[a]=_0f, FZ[a]=_0f; } // TODO: assert unrolled
     // Tension
     for(size_t a: range(3)) { // TODO: assert unrolled
      int e = E[a] = index+D[i%2][a];
      v8sf Rx = X[a] = loadu(Px, e) - Ox;
      v8sf Ry = Y[a] = loadu(Py, e) - Oy;
      v8sf Rz = Z[a] = loadu(Pz, e) - Oz;
      v8sf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      v8sf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      v8sf x = L - internodeLength;
      v8sf fS = tensionStiffness * x;
      v8sf RVx = loadu(Vx, e) - VOx;
      v8sf RVy = loadu(Vy, e) - VOy;
      v8sf RVz = loadu(Vz, e) - VOz;
      v8sf fB = float8(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      v8sf f = fS + fB;
      v8sf tx = f * Nx;
      v8sf ty = f * Ny;
      v8sf tz = f * Nz;
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
     }
     for(int a=0; a<3; a++) {
      int e = E[a];
      storeu(Fx, e, loadu(Fx, e) + FX[a]);
      storeu(Fy, e, loadu(Fy, e) + FY[a]);
      storeu(Fz, e, loadu(Fz, e) + FZ[a]);
     }
     store(Fx, index, load(Fx, index) + FX[3]);
     store(Fy, index, load(Fy, index) + FY[3]);
     store(Fz, index, load(Fz, index) + FZ[3]);
    }
   }

   { // Tension from next row (FIXME: reverse to align)
    size_t i = start+size;
    size_t base = i*stride+simd;
    for(int j=0; j<W; j+=simd) {
     size_t  index = base+j;
     v8sf Ox = load(Px, index);
     v8sf Oy = load(Py, index);
     v8sf Oz = load(Pz, index);
     v8sf VOx = load(Vx, index);
     v8sf VOy = load(Vy, index);
     v8sf VOz = load(Vz, index);

     // Tension
     for(int a=0; a<2; a++) { // TODO: assert unrolled
      int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      v8sf Rx = loadu(Px, e) - Ox;
      v8sf Ry = loadu(Py, e) - Oy;
      v8sf Rz = loadu(Pz, e) - Oz;
      v8sf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      v8sf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      v8sf x = L - internodeLength;
      v8sf fS = tensionStiffness * x;
      v8sf RVx = loadu(Vx, e) - VOx;
      v8sf RVy = loadu(Vy, e) - VOy;
      v8sf RVz = loadu(Vz, e) - VOz;
      v8sf fB = float8(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      v8sf f = fS + fB;
      v8sf tx = f * Nx;
      v8sf ty = f * Ny;
      v8sf tz = f * Nz;
      storeu(Fx, e, loadu(Fx, e) - tx);
      storeu(Fy, e, loadu(Fy, e) - ty);
      storeu(Fz, e, loadu(Fz, e) - tz);
     }
    }
   }
 }, 1 /*FIXME: missing pression on job boundaries*/);
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 membraneIntegrationTime.start();
 const v8sf dt_mass = float8(this->dt / membrane.mass), dt = float8(this->dt);
 v8sf maxMembraneV8 = _0f;
 float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
 float* const pVx = membrane.Vx.begin(), *pVy = membrane.Vy.begin(), *pVz = membrane.Vz.begin();
 float* const pPx = membrane.Px.begin(), *pPy = membrane.Py.begin(), *pPz = membrane.Pz.begin();
 for(size_t i=1; i<membrane.H-1; i+=1) {
  size_t W = membrane.W, stride = membrane.stride;
  // Adds force from repeated nodes
  Fx[i*stride+simd+0] += Fx[i*stride+simd+W];
  Fy[i*stride+simd+0] += Fy[i*stride+simd+W];
  Fz[i*stride+simd+0] += Fz[i*stride+simd+W];
  Fx[i*stride+simd+W-1] += Fx[i*stride+simd-1];
  Fy[i*stride+simd+W-1] += Fy[i*stride+simd-1];
  Fz[i*stride+simd+W-1] += Fz[i*stride+simd-1];
  for(size_t j=0; j<W; j+=simd) {
   size_t k = i*stride+simd+j;
   // Symplectic Euler
   v8sf Vx = load(pVx, k), Vy = load(pVy, k), Vz = load(pVz, k);
   Vx += dt_mass * load(Fx, k);
   Vy += dt_mass * load(Fy, k);
   Vz += dt_mass * load(Fz, k);
   store(pVx, k, Vx);
   store(pVy, k, Vy);
   store(pVz, k, Vz);
   const v8sf Px = load(pPx, k) + dt * Vx;
   const v8sf Py = load(pPy, k) + dt * Vy;
   v8sf Pz = load(pPz, k) + dt * Vz;
   Pz = min(float8(topZ), Pz);
   store(pPx, k, Px);
   store(pPy, k, Py);
   store(pPz, k, Pz);
   maxMembraneV8 = max(maxMembraneV8, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
  }
  // Copies position back to repeated nodes
  pPx[i*stride+simd-1] = pPx[i*stride+simd+W-1];
  pPy[i*stride+simd-1] = pPy[i*stride+simd+W-1];
  pPz[i*stride+simd-1] = pPz[i*stride+simd+W-1];
  pPx[i*stride+simd+W] = pPx[i*stride+simd+0];
  pPy[i*stride+simd+W] = pPy[i*stride+simd+0];
  pPz[i*stride+simd+W] = pPz[i*stride+simd+0];
 }
 float maxMembraneV = 0;
 for(size_t k: range(simd)) maxMembraneV = ::max(maxMembraneV, maxMembraneV8[k]);
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
 membraneIntegrationTime.stop();
}
