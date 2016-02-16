#include "simulation.h"
#include "membrane.h"
#include "parallel.h"

#define MEMBRANE_LATTICE 0
#define MEMBRANE_LATTICE_3 0

static inline void membraneTensionPressure(const float* const Px, const float* const Py, const float* const Pz,
                                           const float* const Vx, const float* const Vy, const float* const Vz,
                                           float* const Fx, float* const Fy, float* const Fz,
                                           const int W, const int margin, const int stride,
                                           const vXsf pressureCoefficient, // [area = length(cross)/2] / 3 vertices
                                           const vXsf internodeLength, const vXsf tensionStiffness, const vXsf tensionDamping,
                                           const int start, const int size
                                           ) {
#define tension(i) \
 const int e##i##j = v+e##i; \
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
  const int e0 = -stride+i%2;
  const int e1 = -stride-!(i%2);
  const int e2 = -1;
  for(int j=0; j<W; j+=simd) {
   int v = base+j;
   const vXsf Ox = load(Px, v);
   const vXsf Oy = load(Py, v);
   const vXsf Oz = load(Pz, v);
   const vXsf VOx = load(Vx, v);
   const vXsf VOy = load(Vy, v);
   const vXsf VOz = load(Vz, v);

   tension(0) tension(1) pressure(0, 1) tension(2) pressure(1, 2);
   const vXsf fx2 = loadu(Fx, e2j) - tx2; storeu(Fx, e2j, fx2);
   const vXsf fy2 = loadu(Fy, e2j) - ty2; storeu(Fy, e2j, fy2);
   const vXsf fz2 = loadu(Fz, e2j)  - tz2; storeu(Fz, e2j, fz2);
   const vXsf fx = load(Fx, v) + tx0 + tx1 + tx2 + ppx01 + ppx12; store(Fx, v, fx);
   const vXsf fy = load(Fy, v) + ty0 + ty1 + ty2 + ppy01 + ppy12; store(Fy, v, fy);
   const vXsf fz = load(Fz, v)  + tz0 + tz1 + tz2  + ppz01 + ppz12; store(Fz, v, fz);
  }
 }

 for(int i=start+1; i<start+size; i++) {
  const int base = margin+i*stride;
  const int e0 = -stride+i%2;
  const int e1 = -stride-!(i%2);
  const int e2 = -1;
  for(int j=0; j<W; j+=simd) {
   int v = base+j;
   const vXsf Ox = load(Px, v);
   const vXsf Oy = load(Py, v);
   const vXsf Oz = load(Pz, v);
   const vXsf VOx = load(Vx, v);
   const vXsf VOy = load(Vy, v);
   const vXsf VOz = load(Vz, v);

   tension(0) tension(1) pressure(0, 1) tension(2) pressure(1, 2);
   const vXsf fx0 = loadu(Fx, e0j) - tx0; storeu(Fx, e0j, fx0);
   const vXsf fy0 = loadu(Fy, e0j) - ty0; storeu(Fy, e0j, fy0);
   const vXsf fz0 = loadu(Fz, e0j)  - tz0; storeu(Fz, e0j, fz0);
   const vXsf fx1 = loadu(Fx, e1j) - tx1; storeu(Fx, e1j, fx1);
   const vXsf fy1 = loadu(Fy, e1j) - ty1; storeu(Fy, e1j, fy1);
   const vXsf fz1 = loadu(Fz, e1j)  - tz1; storeu(Fz, e1j, fz1);
   const vXsf fx2 = loadu(Fx, e2j) - tx2; storeu(Fx, e2j, fx2);
   const vXsf fy2 = loadu(Fy, e2j) - ty2; storeu(Fy, e2j, fy2);
   const vXsf fz2 = loadu(Fz, e2j)  - tz2; storeu(Fz, e2j, fz2);
   const vXsf fx = load(Fx, v) + tx0 + tx1 + tx2 + ppx01 + ppx12; store(Fx, v, fx);
   const vXsf fy = load(Fy, v) + ty0 + ty1 + ty2 + ppy01 + ppy12; store(Fy, v, fy);
   const vXsf fz = load(Fz, v)  + tz0 + tz1 + tz2  + ppz01 + ppz12; store(Fz, v, fz);
  }
 }

 // Tension from next row
 {
  const int i = start+size-1;
  const int base = margin+i*stride;
  const int e3 = stride-!(i%2);
  const int e4 = stride+(i%2);
  for(int j=0; j<W; j+=simd) {
   int v = base+j;
   const vXsf Ox = load(Px, v);
   const vXsf Oy = load(Py, v);
   const vXsf Oz = load(Pz, v);
   const vXsf VOx = load(Vx, v);
   const vXsf VOy = load(Vy, v);
   const vXsf VOz = load(Vz, v);

   // Tension
   tension(3) tension(4);
   const vXsf fx = load(Fx, v) + tx3 + tx4; store(Fx, v, fx);
   const vXsf fy = load(Fy, v) + ty3 + ty4; store(Fy, v, fy);
   const vXsf fz = load(Fz, v)  + tz3 + tz4; store(Fz, v, fz);
  }
 }
#undef tension
#undef pressure
}

void Simulation::stepMembrane() {
 size_t stride = membrane->stride;
 membraneInitializationTime += parallel_chunk(stride/simd, (membrane->count-stride)/simd,
                  [this](uint, uint start, uint size) {
    float* const Fx = membrane->Fx.begin(), *Fy = membrane->Fy.begin(), *Fz = membrane->Fz.begin();
    for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     store(Fx, i, _0f);
     store(Fy, i, _0f);
     store(Fz, i, _0f);
    }
  });

 //membraneForceTime += parallel_for(1, membrane->H-1, [this](uint, int index) {
 membraneForceTime += parallel_chunk(1, membrane->H-1, [this](uint, int start, int size) {
   membraneTensionPressure(
    membrane->Px.data, membrane->Py.data, membrane->Pz.data,
    membrane->Vx.data, membrane->Vy.data, membrane->Vz.data,
    membrane->Fx.begin(), membrane->Fy.begin(), membrane->Fz.begin(),
    membrane->W, membrane->margin, membrane->stride,
    floatX(pressure/2) /*[area = length(cross)/2]*/, floatX(membrane->internodeLength),
    floatX(membrane->tensionStiffness), floatX(membrane->tensionDamping),
    //index, 1);
    start, size);
 });
}

void Simulation::stepMembraneIntegration() {
 if(!membrane->count) return;
 float maxMembraneV2 = 0;
 if(membraneViscosity) {
  const size_t threadCount = ::threadCount();
  float maxMembraneVT2_[threadCount]; mref<float>(maxMembraneVT2_, threadCount).clear(0);
  float* const maxMembraneVT2 = maxMembraneVT2_;
  membraneIntegrationTime += parallel_for(1, membrane->H-1, [this, maxMembraneVT2](uint id, uint i) {
#if GEAR
   const vXsf _1_mass = floatX(1 / membrane->mass);
   const vXsf p[3] = {floatX(dt), floatX(dt*dt/2), floatX(dt*dt*dt/6)};
   const vXsf c[4] = {floatX(1./6), floatX(5/(6*dt)), floatX(1/(dt*dt)), floatX(1/(3*dt*dt*dt))};
   float* const pPDx0 = membrane->PDx[0].begin(), *pPDy0 = membrane->PDy[0].begin(), *pPDz0 = membrane->PDz[0].begin();
   float* const pPDx1 = membrane->PDx[1].begin(), *pPDy1 = membrane->PDy[1].begin(), *pPDz1 = membrane->PDz[1].begin();
#else
   const vXsf dt_mass = floatX(this->dt / membrane->mass), dt = floatX(this->dt);
#endif
   const vXsf membraneViscosity = floatX(this->membraneViscosity);
   vXsf maxMembraneVX2 = _0f;
   float* const pFx = membrane->Fx.begin(), *pFy = membrane->Fy.begin(), *pFz = membrane->Fz.begin();
   float* const pVx = membrane->Vx.begin(), *pVy = membrane->Vy.begin(), *pVz = membrane->Vz.begin();
   float* const pPx = membrane->Px.begin(), *pPy = membrane->Py.begin(), *pPz = membrane->Pz.begin();
   const int W = membrane->W, stride = membrane->stride, margin=membrane->margin;
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
#if GEAR
   // 4th order "Gear" BDF approximated using a predictor
   vXsf PDx0 = load(pPDx0, k), PDy0 = load(pPDy0, k), PDz0 = load(pPDz0, k);
   vXsf PDx1 = load(pPDx1, k), PDy1 = load(pPDy1, k), PDz1 = load(pPDz1, k);

   const vXsf Rx = p[1] * (Fx * _1_mass - PDx0);
   const vXsf Ry = p[1] * (Fy * _1_mass - PDy0);
   const vXsf Rz = p[1] * (Fz * _1_mass - PDz0);
   // "Correction"
   Px += c[0]*Rx;
   Py += c[0]*Ry;
   Pz += c[0]*Rz;

   Vx += c[1]*Rx;
   Vy += c[1]*Ry;
   Vz += c[1]*Rz;
   Vx *= membraneViscosity;
   Vy *= membraneViscosity;
   Vz *= membraneViscosity;

   PDx0 += c[2]*Rx;
   PDy0 += c[2]*Ry;
   PDz0 += c[2]*Rz;

   PDx1 += c[3]*Rx;
   PDy1 += c[3]*Ry;
   PDz1 += c[3]*Rz;

   // "Prediction"
   Px += p[0]*Vx;
   Py += p[0]*Vy;
   Pz += p[0]*Vz;

   Px += p[1]*PDx0;
   Py += p[1]*PDy0;
   Pz += p[1]*PDz0;

   Px += p[2]*PDx1;
   Py += p[2]*PDy1;
   Pz += p[2]*PDz1;

   Vx += p[0]*PDx0;
   Vy += p[0]*PDy0;
   Vz += p[0]*PDz0;

   Vx += p[1]*PDx1;
   Vy += p[1]*PDy1;
   Vz += p[1]*PDz1;

   PDx0 += p[0]*PDx1;
   PDy0 += p[0]*PDy1;
   PDz0 += p[0]*PDz1;

   store(pPDx0, k, PDx0); store(pPDy0, k, PDy0); store(pPDz0, k, PDz0);
   store(pPDx1, k, PDx1); store(pPDy1, k, PDy1); store(pPDz1, k, PDz1);
#else
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
#endif
    store(pVx, k, Vx); store(pVy, k, Vy); store(pVz, k, Vz);
    store(pPx, k, Px); store(pPy, k, Py); store(pPz, k, Pz);
    maxMembraneVX2 = max(maxMembraneVX2, Vx*Vx + Vy*Vy + Vz*Vz);
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
   maxMembraneVT2[id] = max(maxMembraneVT2[id], max(maxMembraneVX2));
  });
  for(int k: range(threadCount)) maxMembraneV2 = ::max(maxMembraneV2, maxMembraneVT2[k]);
 }
 float maxGrainMembraneV = maxGrainV + sqrt(maxMembraneV2);
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}
