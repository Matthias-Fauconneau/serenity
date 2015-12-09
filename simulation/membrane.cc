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

 static unused bool once = ({ log(membrane.H); true; });
 tsc membraneForceTime;
 //membraneForceTime += parallel_chunk(1, membrane.H-1, [this](uint, size_t start, size_t size) {
 //membraneForceTime += parallel_for(1, membrane.H-1, [this](uint, size_t index) { const size_t start = index, size = 1;
 /*ref<float> Px = membrane.Px, Py = membrane.Py, Pz = membrane.Pz;
 ref<float> Vx = membrane.Vx, Vy = membrane.Vy, Vz = membrane.Vz;
 mref<float> Fx = membrane.Fx, Fy = membrane.Fy, Fz = membrane.Fz;*/
 const float* const Px = membrane.Px.data, *Py = membrane.Py.data, *Pz = membrane.Pz.data;
 const float* const Vx = membrane.Vx.data, *Vy = membrane.Vy.data, *Vz = membrane.Vz.data;
 float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();

 int D[2][6];
 int dy[6] {-1, -1,            0,       1,         1,     0};
 int dx[2][6] {{0, -1, -1, -1, 0, 1},{1, 0, -1, 0, 1, 1}};
 size_t stride = membrane.stride, W = membrane.W, margin=membrane.margin;
 for(int i=0; i<2; i++) for(int a=0; a<6; a++) D[i][a] = dy[a]*(int)stride+dx[i][a];
 const vXsf unused P = floatX(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
 const vXsf internodeLength = floatX(membrane.internodeLength);
 const vXsf tensionStiffness = floatX(membrane.tensionStiffness);

 //#pragma omp parallel for
 for(int index=1; index<int(membrane.H-1); index++) { const size_t start = index, size = 1;
   // Tension from previous row
   {
    const size_t i = start;
    size_t base = i*stride+margin;
    for(size_t j=0; j<W; j+=simd) {
     size_t index = base+j;
     const vXsf Ox = load(Px, index);
     const vXsf Oy = load(Py, index);
     const vXsf Oz = load(Pz, index);
     const vXsf VOx = load(Vx, index);
     const vXsf VOy = load(Vy, index);
     const vXsf VOz = load(Vz, index);

     // Tension
     vXsf fx = _0f, fy = _0f, fz = _0f; // Assumes accumulators stays in registers
     for(size_t a=0; a<3; a++) { // TODO: assert unrolled
      size_t e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      const vXsf Rx = loadu(Px, e) - Ox;
      const vXsf Ry = loadu(Py, e) - Oy;
      const vXsf Rz = loadu(Pz, e) - Oz;
      const vXsf sqL = Rx*Rx+Ry*Ry+Rz*Rz;
      const vXsf L = sqrt(sqL);
      const vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      const vXsf x = L - internodeLength;
      const vXsf fS = tensionStiffness * x;
      const vXsf RVx = loadu(Vx, e) - VOx;
      const vXsf RVy = loadu(Vy, e) - VOy;
      const vXsf RVz = loadu(Vz, e) - VOz;
      const vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      const vXsf f = fS + fB;
      const vXsf tx = f * Nx;
      const vXsf ty = f * Ny;
      const vXsf tz = f * Nz;
      fx += tx;
      fy += ty;
      fz += tz;
      if(a==2) { // FIXME: assert peeled
       const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
       const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
       const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
      }
     }
     // TODO: pression
     const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
     const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
     const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
    }
   }

   /*for(size_t i=start+1; i<start+size; i++) {
    size_t base = i*stride+margin;
    for(size_t j=0; j<W; j+=simd) {
     size_t index = base+j;
     const vXsf Ox = load(Px, index);
     const vXsf Oy = load(Py, index);
     const vXsf Oz = load(Pz, index);
     const vXsf VOx = load(Vx, index);
     const vXsf VOy = load(Vy, index);
     const vXsf VOz = load(Vz, index);

     uint E[3];
     vXsf X[3], Y[3], Z[3];
     vXsf FX[4], FY[4], FZ[4];
     for(size_t a: range(4)) { FX[a]=_0f, FY[a]=_0f, FZ[a]=_0f; } // TODO: assert unrolled
     // Tension
     for(size_t a: range(3)) { // TODO: assert unrolled
      size_t e = E[a] = index+D[i%2][a];
      const vXsf Rx = X[a] = loadu(Px, e) - Ox;
      const vXsf Ry = Y[a] = loadu(Py, e) - Oy;
      const vXsf Rz = Z[a] = loadu(Pz, e) - Oz;
      const vXsf sqL = Rx*Rx+Ry*Ry+Rz*Rz;
      const vXsf L = sqrt(sqL);
      const vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      const vXsf x = L - internodeLength;
      const vXsf fS = tensionStiffness * x;
      const vXsf RVx = loadu(Vx, e) - VOx;
      const vXsf RVy = loadu(Vy, e) - VOy;
      const vXsf RVz = loadu(Vz, e) - VOz;
      const vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      const vXsf f = fS + fB;
      const vXsf tx = f * Nx;
      const vXsf ty = f * Ny;
      const vXsf tz = f * Nz;
      FX[a] -= tx;
      FY[a] -= ty;
      FZ[a] -= tz;
      FX[3] += tx;
      FY[3] += ty;
      FZ[3] += tz;
     }
     #if 0
     for(size_t a: range(2)) {
      size_t b = a+1; // TODO: Assert peeled
      const vXsf px = (Y[a]*Z[b] - Y[b]*Z[a]);
      const vXsf py = (Z[a]*X[b] - Z[b]*X[a]);
      const vXsf pz = (X[a]*Y[b] - X[b]*Y[a]);
      const vXsf ppx = P * px;
      const vXsf ppy = P * py;
      const vXsf ppz = P * pz;
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
     #endif
     for(int a=0; a<3; a++) {
      size_t e = E[a];
      const vXsf dFx = loadu(Fx, e) + FX[a]; storeu(Fx, e, dFx);
      const vXsf dFy = loadu(Fy, e) + FY[a]; storeu(Fy, e, dFy);
      const vXsf dFz = loadu(Fz, e) + FZ[a]; storeu(Fz, e, dFz);
     }
     const vXsf dFx = load(Fx, index) + FX[3]; store(Fx, index, dFx);
     const vXsf dFy = load(Fy, index) + FY[3]; store(Fy, index, dFy);
     const vXsf dFz = load(Fz, index) + FZ[3]; store(Fz, index, dFz);
    }
   }*/

   /*{ // Tension from next row
    size_t i = start+size-1;
    size_t base = i*stride+margin;
    for(size_t j=0; j<W; j+=simd) {
     size_t  index = base+j;
     const vXsf Ox = load(Px, index);
     const vXsf Oy = load(Py, index);
     const vXsf Oz = load(Pz, index);
     const vXsf VOx = load(Vx, index);
     const vXsf VOy = load(Vy, index);
     const vXsf VOz = load(Vz, index);

     // Tension
     vXsf fx = _0f, fy = _0f, fz = _0f; // Assumes accumulators stays in registers
     for(size_t a: range(3, 5)) { // TODO: assert unrolled
      size_t e = index+D[i%2][a]; // Gather (TODO: assert reduced i%2)
      const vXsf Rx = loadu(Px, e) - Ox;
      const vXsf Ry = loadu(Py, e) - Oy;
      const vXsf Rz = loadu(Pz, e) - Oz;
      const vXsf sqL = Rx*Rx+Ry*Ry+Rz*Rz;
      const vXsf L = sqrt(sqL);
      const vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
      const vXsf x = L - internodeLength;
      const vXsf fS = tensionStiffness * x;
      const vXsf RVx = loadu(Vx, e) - VOx;
      const vXsf RVy = loadu(Vy, e) - VOy;
      const vXsf RVz = loadu(Vz, e) - VOz;
      const vXsf fB = floatX(membrane.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
      const vXsf f = fS + fB;
      const vXsf tx = f * Nx;
      const vXsf ty = f * Ny;
      const vXsf tz = f * Nz;
      fx += tx;
      fy += ty;
      fz += tz;
     }
     const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
     const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
     const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
    }
   }*/
 }//, 1 /*FIXME: missing pression on job boundaries*/);
 this->membraneForceTime += membraneForceTime.cycleCount();
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
  size_t W = membrane.W, stride = membrane.stride, margin=membrane.margin;
  // Adds force from repeated nodes
  Fx[i*stride+margin+0] += Fx[i*stride+margin+W];
  Fy[i*stride+margin+0] += Fy[i*stride+margin+W];
  Fz[i*stride+margin+0] += Fz[i*stride+margin+W];
  Fx[i*stride+margin+W-1] += Fx[i*stride+margin-1];
  Fy[i*stride+margin+W-1] += Fy[i*stride+margin-1];
  Fz[i*stride+margin+W-1] += Fz[i*stride+margin-1];
  for(size_t j=0; j<W; j+=simd) {
   size_t k = i*stride+margin+j;
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
  pPx[i*stride+margin-1] = pPx[i*stride+margin+W-1];
  pPy[i*stride+margin-1] = pPy[i*stride+margin+W-1];
  pPz[i*stride+margin-1] = pPz[i*stride+margin+W-1];
  pPx[i*stride+margin+W] = pPx[i*stride+margin+0];
  pPy[i*stride+margin+W] = pPy[i*stride+margin+0];
  pPz[i*stride+margin+W] = pPz[i*stride+margin+0];
 }
 float maxMembraneV = 0;
 for(size_t k: range(simd)) maxMembraneV = ::max(maxMembraneV, extract(maxMembraneVX,k));
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
 membraneIntegrationTime.stop();
}
