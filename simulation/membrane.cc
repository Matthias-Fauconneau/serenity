#include "simulation.h"
#include "parallel.h"

static inline void membraneTensionPressure(const float* const Px, const float* const Py, const float* const Pz,
                                           const float* const Vx, const float* const Vy, const float* const Vz,
                                           float* const Fx, float* const Fy, float* const Fz,
                                           const int W, const int margin, const int stride,
                                           const vXsf pressureCoefficient, // [area = length(cross)/2] / 3 vertices
                                           const vXsf internodeLength, const vXsf tensionStiffness, const vXsf tensionDamping,
                                           const int start, const int size
                                           ) {
#define tension(i) \
 const int e##i##j = e##i+j; \
 const vXsf Rx##i = loadu(Px, e##i##j) - Ox; \
 const vXsf Ry##i = loadu(Py, e##i##j) - Oy; \
 const vXsf Rz##i = loadu(Pz, e##i##j) - Oz; \
 const vXsf sqL##i = Rx##i*Rx##i+Ry##i*Ry##i+Rz##i*Rz##i; \
 const vXsf L##i = sqrt(sqL##i); \
 const vXsf Nx##i = Rx##i/L##i, Ny##i = Ry##i/L##i, Nz##i = Rz##i/L##i; \
 const vXsf x##i = L##i - internodeLength; \
 const vXsf fS##i = tensionStiffness * x##i; \
 const vXsf RVx##i = loadu(Vx, e##i##j) - VOx; \
 const vXsf RVy##i = loadu(Vy, e##i##j) - VOy; \
 const vXsf RVz##i = loadu(Vz, e##i##j) - VOz; \
 const vXsf fB##i = tensionDamping * (Nx##i * RVx##i + Ny##i * RVy##i + Nz##i * RVz##i); \
 const vXsf f##i = fS##i + fB##i; \
 const vXsf tx##i = f##i * Nx##i; \
 const vXsf ty##i = f##i * Ny##i; \
 const vXsf tz##i = f##i * Nz##i;
#define pressure(a, b) \
 const vXsf px##a##b = (Ry##a*Rz##b - Ry##b*Rz##a); \
 const vXsf py##a##b = (Rz##a*Rx##b - Rz##b*Rx##a); \
 const vXsf pz##a##b = (Rx##a*Ry##b - Rx##b*Ry##a); \
 const vXsf ppx##a##b = pressureCoefficient * px##a##b; \
 const vXsf ppy##a##b = pressureCoefficient * py##a##b; \
 const vXsf ppz##a##b = pressureCoefficient * pz##a##b;

 // Previous row (same as loop on inner domain but without writing to previous row (done by next row ending)
 {
  const int i = start;
  const int base = margin+i*stride;
  const int e0 = base-stride+i%2;
  const int e1 = base-stride-!(i%2);
  const int e2 = base-1;
  for(int j=0; j<W; j+=simd) {
   int index = base+j;
   const vXsf Ox = load(Px, index);
   const vXsf Oy = load(Py, index);
   const vXsf Oz = load(Pz, index);
   const vXsf VOx = load(Vx, index);
   const vXsf VOy = load(Vy, index);
   const vXsf VOz = load(Vz, index);

   tension(0) tension(1) pressure(0, 1) tension(2) pressure(1, 2);
   const vXsf fx2 = loadu(Fx, e2j) - tx2 + ppx12; storeu(Fx, e2j, fx2);
   const vXsf fy2 = loadu(Fy, e2j) - ty2 + ppy12; storeu(Fy, e2j, fy2);
   const vXsf fz2 = loadu(Fz, e2j)  - tz2 + ppz12; storeu(Fz, e2j, fz2);
   const vXsf fx = load(Fx, index) + tx0 + tx1 + tx2 + ppx01 + ppx12; store(Fx, index, fx);
   const vXsf fy = load(Fy, index) + ty0 + ty1 + ty2 + ppy01 + ppy12; store(Fy, index, fy);
   const vXsf fz = load(Fz, index)  + tz0 + tz1 + tz2  + ppz01 + ppz12; store(Fz, index, fz);
  }
 }

 for(int i=start+1; i<start+size; i++) {
  const int base = margin+i*stride;
  const int e0 = base-stride+i%2;
  const int e1 = base-stride-!(i%2);
  const int e2 = base-1;
  for(int j=0; j<W; j+=simd) {
   int index = base+j;
   const vXsf Ox = load(Px, index);
   const vXsf Oy = load(Py, index);
   const vXsf Oz = load(Pz, index);
   const vXsf VOx = load(Vx, index);
   const vXsf VOy = load(Vy, index);
   const vXsf VOz = load(Vz, index);

   tension(0) tension(1) pressure(0, 1) tension(2) pressure(1, 2);
   const vXsf fx0 = loadu(Fx, e0j) - tx0 + ppx01; storeu(Fx, e0j, fx0);
   const vXsf fy0 = loadu(Fy, e0j) - ty0 + ppy01; storeu(Fy, e0j, fy0);
   const vXsf fz0 = loadu(Fz, e0j)  - tz0 + ppz01; storeu(Fz, e0j, fz0);
   const vXsf fx1 = loadu(Fx, e1j) - tx1 + ppx01 + ppx12; storeu(Fx, e1j, fx1);
   const vXsf fy1 = loadu(Fy, e1j) - ty1 + ppy01 + ppy12; storeu(Fy, e1j, fy1);
   const vXsf fz1 = loadu(Fz, e1j)  - tz1 + ppz01 + ppz12; storeu(Fz, e1j, fz1);
   const vXsf fx2 = loadu(Fx, e2j) - tx2 + ppx12; storeu(Fx, e2j, fx2);
   const vXsf fy2 = loadu(Fy, e2j) - ty2 + ppy12; storeu(Fy, e2j, fy2);
   const vXsf fz2 = loadu(Fz, e2j)  - tz2 + ppz12; storeu(Fz, e2j, fz2);
   const vXsf fx = load(Fx, index) + tx0 + tx1 + tx2 + ppx01 + ppx12; store(Fx, index, fx);
   const vXsf fy = load(Fy, index) + ty0 + ty1 + ty2 + ppy01 + ppy12; store(Fy, index, fy);
   const vXsf fz = load(Fz, index)  + tz0 + tz1 + tz2  + ppz01 + ppz12; store(Fz, index, fz);
  }
 }

 // Tension from next row
 {
  const int i = start+size-1;
  const int base = margin+i*stride;
  const int e3 = base+stride-!(i%2);
  const int e4 = base+stride+(i%2);
  for(int j=0; j<W; j+=simd) {
   int index = base+j;
   const vXsf Ox = load(Px, index);
   const vXsf Oy = load(Py, index);
   const vXsf Oz = load(Pz, index);
   const vXsf VOx = load(Vx, index);
   const vXsf VOy = load(Vy, index);
   const vXsf VOz = load(Vz, index);

   // Tension
   tension(3) tension(4);
   const vXsf fx = load(Fx, index) + tx3 + tx4; store(Fx, index, fx);
   const vXsf fy = load(Fy, index) + ty3 + ty4; store(Fy, index, fy);
   const vXsf fz = load(Fz, index)  + tz3 + tz4; store(Fz, index, fz);
  }
 }
#undef tension
#undef pressure
}

void Simulation::stepMembrane() {
 size_t stride = membrane.stride;
 membraneInitializationTime += parallel_chunk(stride/simd, (membrane.count-stride)/simd,
                  [this](uint, uint start, uint size) {
    float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
    for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     store(Fx, i, _0f);
     store(Fy, i, _0f);
     store(Fz, i, _0f);
    }
  });

 //membraneForceTime += parallel_for(1, membrane.H-1, [this](uint, int index) {
 membraneForceTime += parallel_chunk(1, membrane.H-1, [this](uint, int start, int size) {
   membraneTensionPressure(
    membrane.Px.data, membrane.Py.data, membrane.Pz.data,
    membrane.Vx.data, membrane.Vy.data, membrane.Vz.data,
    membrane.Fx.begin(), membrane.Fy.begin(), membrane.Fz.begin(),
    membrane.W, membrane.margin, membrane.stride,
    floatX(pressure/(2*3)) /*[area = length(cross)/2] / 3 vertices*/, floatX(membrane.internodeLength),
    floatX(membrane.tensionStiffness), floatX(membrane.tensionDamping),
    //index, 1);
    start, size);
 });
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 float maxMembraneV = 0;
 if(processState >= ProcessState::Pressure) {
  const size_t threadCount = ::threadCount();
  float maxMembraneV_[threadCount]; mref<float>(maxMembraneV_, threadCount).clear(0);
  membraneIntegrationTime += parallel_for(1, membrane.H-1, [this, &maxMembraneV_](uint id, uint i) {
  const vXsf dt_mass = floatX(this->dt / membrane.mass), dt = floatX(this->dt);
   const vXsf membraneViscosity = floatX(this->membraneViscosity);
   vXsf maxMembraneVX = _0f;
   float* const pFx = membrane.Fx.begin(), *pFy = membrane.Fy.begin(), *pFz = membrane.Fz.begin();
   float* const pVx = membrane.Vx.begin(), *pVy = membrane.Vy.begin(), *pVz = membrane.Vz.begin();
   float* const pPx = membrane.Px.begin(), *pPy = membrane.Py.begin(), *pPz = membrane.Pz.begin();
   const int W = membrane.W, stride = membrane.stride, margin=membrane.margin;
   // Adds force from repeated nodes
   pFx[i*stride+margin+0] += pFx[i*stride+margin+W];
   pFy[i*stride+margin+0] += pFy[i*stride+margin+W];
   pFz[i*stride+margin+0] += pFz[i*stride+margin+W];
   pFx[i*stride+margin+W-1] += pFx[i*stride+margin-1];
   pFy[i*stride+margin+W-1] += pFy[i*stride+margin-1];
   pFz[i*stride+margin+W-1] += pFz[i*stride+margin-1];
   int base = margin+i*stride;
   for(int j=0; j<W; j+=simd) {
    int k = base+j;
    const vXsf Fx = load(pFx, k), Fy = load(pFy, k), Fz = load(pFz, k);
    vXsf Vx = load(pVx, k), Vy = load(pVy, k), Vz = load(pVz, k);
    vXsf Px = load(pPx, k), Py = load(pPy, k), Pz = load(pPz, k);
    // Symplectic Euler
    Vx += dt_mass * Fx;
    Vy += dt_mass * Fy;
    Vz += dt_mass * Fz;
    Vx *= membraneViscosity;
    Vy *= membraneViscosity;
    Vz *= membraneViscosity;
    Px += dt * Vx;
    Py += dt * Vy;
    Pz += dt * Vz;
    store(pVx, k, Vx); store(pVy, k, Vy); store(pVz, k, Vz);
    store(pPx, k, Px); store(pPy, k, Py); store(pPz, k, Pz);
    maxMembraneVX = max(maxMembraneVX, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
   }
   // Copies position back to repeated nodes
   pPx[i*stride+margin-1] = pPx[i*stride+margin+W-1];
   pPy[i*stride+margin-1] = pPy[i*stride+margin+W-1];
   pPz[i*stride+margin-1] = pPz[i*stride+margin+W-1];
   pPx[i*stride+margin+W] = pPx[i*stride+margin+0];
   pPy[i*stride+margin+W] = pPy[i*stride+margin+0];
   pPz[i*stride+margin+W] = pPz[i*stride+margin+0];
   // Copies velocities back to repeated nodes
   pVx[i*stride+margin-1] = pVx[i*stride+margin+W-1];
   pVy[i*stride+margin-1] = pVy[i*stride+margin+W-1];
   pVz[i*stride+margin-1] = pVz[i*stride+margin+W-1];
   pVx[i*stride+margin+W] = pVx[i*stride+margin+0];
   pVy[i*stride+margin+W] = pVy[i*stride+margin+0];
   pVz[i*stride+margin+W] = pVz[i*stride+margin+0];
   maxMembraneV_[id] = max(maxMembraneV_[id], max(maxMembraneVX));
  });
  for(int k: range(threadCount)) maxMembraneV = ::max(maxMembraneV, maxMembraneV_[k]);
 }
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}
