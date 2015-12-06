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
   vXsf P = floatX(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
   vXsf internodeLength = floatX(membrane.internodeLength);
   vXsf tensionStiffness = floatX(membrane.tensionStiffness);

   // Tension from previous row
   {
    size_t i = start;
    int base = i*stride+simd;
    for(int j=0; j<W; j+=simd) {
     int index = base+j;
     vXsf Ox = load(Px, index);
     vXsf Oy = load(Py, index);
     vXsf Oz = load(Pz, index);
     vXsf VOx = load(Vx, index);
     vXsf VOy = load(Vy, index);
     vXsf VOz = load(Vz, index);

     // Tension
     vXsf fx = _0f, fy = _0f, fz = _0f; // Assumes accumulators stays in registers
     for(int a=0; a<2; a++) { // TODO: assert unrolled
      int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      vXsf Rx = loadu(Px, e) - Ox;
      vXsf Ry = loadu(Py, e) - Oy;
      vXsf Rz = loadu(Pz, e) - Oz;
      vXsf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      vXsf x = L - internodeLength;
      vXsf fS = tensionStiffness * x;
      vXsf RVx = loadu(Vx, e) - VOx;
      vXsf RVy = loadu(Vy, e) - VOy;
      vXsf RVz = loadu(Vz, e) - VOz;
      vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      vXsf f = fS + fB;
      vXsf tx = f * Nx;
      vXsf ty = f * Ny;
      vXsf tz = f * Nz;
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
     vXsf Ox = load(Px, index);
     vXsf Oy = load(Py, index);
     vXsf Oz = load(Pz, index);
     vXsf VOx = load(Vx, index);
     vXsf VOy = load(Vy, index);
     vXsf VOz = load(Vz, index);

     int E[3];
     vXsf X[3], Y[3], Z[3];
     vXsf FX[4], FY[4], FZ[4];
     for(size_t a: range(4)) { FX[a]=_0f, FY[a]=_0f, FZ[a]=_0f; } // TODO: assert unrolled
     // Tension
     for(size_t a: range(3)) { // TODO: assert unrolled
      int e = E[a] = index+D[i%2][a];
      vXsf Rx = X[a] = loadu(Px, e) - Ox;
      vXsf Ry = Y[a] = loadu(Py, e) - Oy;
      vXsf Rz = Z[a] = loadu(Pz, e) - Oz;
      vXsf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      vXsf x = L - internodeLength;
      vXsf fS = tensionStiffness * x;
      vXsf RVx = loadu(Vx, e) - VOx;
      vXsf RVy = loadu(Vy, e) - VOy;
      vXsf RVz = loadu(Vz, e) - VOz;
      vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      vXsf f = fS + fB;
      vXsf tx = f * Nx;
      vXsf ty = f * Ny;
      vXsf tz = f * Nz;
      FX[a] -= tx;
      FY[a] -= ty;
      FZ[a] -= tz;
      FX[3] += tx;
      FY[3] += ty;
      FZ[3] += tz;
     }
     for(size_t a: range(2)) {
      int b = a+1; // TODO: Assert peeled
      vXsf px = (Y[a]*Z[b] - Y[b]*Z[a]);
      vXsf py = (Z[a]*X[b] - Z[b]*X[a]);
      vXsf pz = (X[a]*Y[b] - X[b]*Y[a]);
      vXsf ppx = P * px;
      vXsf ppy = P * py;
      vXsf ppz = P * pz;
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
      store/*u*/(Fx, e, loadu(Fx, e) + FX[a]);
      store/*u*/(Fy, e, loadu(Fy, e) + FY[a]);
      store/*u*/(Fz, e, loadu(Fz, e) + FZ[a]);
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
     vXsf Ox = load(Px, index);
     vXsf Oy = load(Py, index);
     vXsf Oz = load(Pz, index);
     vXsf VOx = load(Vx, index);
     vXsf VOy = load(Vy, index);
     vXsf VOz = load(Vz, index);

     // Tension
     for(int a=0; a<2; a++) { // TODO: assert unrolled
      int e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      vXsf Rx = loadu(Px, e) - Ox;
      vXsf Ry = loadu(Py, e) - Oy;
      vXsf Rz = loadu(Pz, e) - Oz;
      vXsf L = sqrt(Rx*Rx+Ry*Ry+Rz*Rz);
      vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      vXsf x = L - internodeLength;
      vXsf fS = tensionStiffness * x;
      vXsf RVx = loadu(Vx, e) - VOx;
      vXsf RVy = loadu(Vy, e) - VOy;
      vXsf RVz = loadu(Vz, e) - VOz;
      vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      vXsf f = fS + fB;
      vXsf tx = f * Nx;
      vXsf ty = f * Ny;
      vXsf tz = f * Nz;
      store/*u*/(Fx, e, loadu(Fx, e) - tx);
      store/*u*/(Fy, e, loadu(Fy, e) - ty);
      store/*u*/(Fz, e, loadu(Fz, e) - tz);
     }
    }
   }
 }, 1 /*FIXME: missing pression on job boundaries*/);
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 membraneIntegrationTime.start();
 const vXsf dt_mass = floatX(this->dt / membrane.mass), dt = floatX(this->dt);
 vXsf maxMembraneVX = _0f;
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
   vXsf Vx = load(pVx, k), Vy = load(pVy, k), Vz = load(pVz, k);
   Vx += dt_mass * load(Fx, k);
   Vy += dt_mass * load(Fy, k);
   Vz += dt_mass * load(Fz, k);
   store(pVx, k, Vx);
   store(pVy, k, Vy);
   store(pVz, k, Vz);
   const vXsf Px = load(pPx, k) + dt * Vx;
   const vXsf Py = load(pPy, k) + dt * Vy;
   vXsf Pz = load(pPz, k) + dt * Vz;
   Pz = min(floatX(topZ), Pz);
   store(pPx, k, Px);
   store(pPy, k, Py);
   store(pPz, k, Pz);
   maxMembraneVX = max(maxMembraneVX, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
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
 for(size_t k: range(simd)) maxMembraneV = ::max(maxMembraneV, extract(maxMembraneVX,k));
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
 membraneIntegrationTime.stop();
}
