#include "simulation.h"
#include "parallel.h"

void Simulation::stepGrain() {
 const vXsf m_Gz = floatX(Grain::mass * Gz);
 grainTime += parallel_chunk(align(simd, grain.count)/simd, [&](uint, size_t start, size_t size) {
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
  store(grain.Fx, i, _0f);
  store(grain.Fy, i, _0f);
  store(grain.Fz, i, m_Gz);
  store(grain.Tx, i, _0f);
  store(grain.Ty, i, _0f);
  store(grain.Tz, i, _0f);
 }
});
}

void Simulation::grainLattice() {
 if(validGrainLattice) return;
 int* const cells = lattice.cells.begin();
 int size = lattice.cells.size;
 for(int i=0; i<size; i++) cells[i] = -1;

 auto scatter = [this](uint, uint start, uint size) {
  const float* const gPx = grain.Px.data+simd, *gPy = grain.Py.data+simd, *gPz = grain.Pz.data+simd;
  int* const base = lattice.base.begin();
  const vXsf scale = floatX(lattice.scale);
  const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
  const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
   vXsi a = intX(i)+_seqi;
   const vXsf Ax = load(gPx, i), Ay = load(gPy, i), Az = load(gPz, i);
   vXsi index = convert(scale*(Az-minZ)) * sizeYX
     + convert(scale*(Ay-minY)) * sizeX
     + convert(scale*(Ax-minX));
   ::scatter(base, index, a);
  }
 };
 if(grain.count/simd) grainLatticeTime += parallel_chunk(grain.count/simd, scatter);
 if(grain.count%simd) {
  const float* const gPx = grain.Px.data+simd, *gPy = grain.Py.data+simd, *gPz = grain.Pz.data+simd;
  int* const base = lattice.base.begin();
  const vXsf scale = floatX(lattice.scale);
  const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
  const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
  uint i=grain.count/simd*simd;
  vXsi a = intX(i)+_seqi;
  const vXsf Ax = load(gPx, i), Ay = load(gPy, i), Az = load(gPz, i);
  vXsi index = convert(scale*(Az-minZ)) * sizeYX
    + convert(scale*(Ay-minY)) * sizeX
    + convert(scale*(Ax-minX));
  for(int k: range(grain.count-i)) base[extract(index, k)] = extract(a, k);
 }

 validGrainLattice = true;
}

void Simulation::stepGrainIntegration() {
 if(!grain.count) return;
 for(size_t i: range(grain.count, align(simd, grain.count))) { grain.Fx[i] = 0; grain.Fy[i] = 0; grain.Fz[i] = 0; }
 const/*expr*/ size_t threadCount = ::threadCount();
 float maxGrainV_[threadCount]; mref<float>(maxGrainV_, threadCount).clear(0);
 grainIntegrationTime += parallel_chunk(align(simd, grain.count)/simd, [this, &maxGrainV_](uint id, size_t start, size_t size) {
#if EULER
  const vXsf dt_mass = floatX(dt / grain.mass), dt = floatX(this->dt);
#endif
  const vXsf dt_2 = floatX(this->dt / 2);
  const vXsf dt_angularMass = floatX(this->dt / Grain::angularMass);
  vXsf maxGrainVX = _0f;
  const float* pFx = grain.Fx.data, *pFy = grain.Fy.data, *pFz = grain.Fz.data;
#if NEWMARK
  const float* pFx0 = grain.Fx.data, *pFy0 = grain.Fy.data, *pFz0 = grain.Fz.data;
  const vXsf dt = floatX(this->dt), dt_2mass = floatX(this->dt / (2*grain.mass));
  const vXsf dt2_4m = floatX(sq(this->dt) / (4*grain.mass));
#endif
  const float* pTx = grain.Tx.begin(), *pTy = grain.Ty.begin(), *pTz = grain.Tz.begin();
  float* const pVx = grain.Vx.begin(), *pVy = grain.Vy.begin(), *pVz = grain.Vz.begin();
  float* const pPx = grain.Px.begin()+simd, *pPy = grain.Py.begin()+simd, *pPz = grain.Pz.begin()+simd;
#if GEAR
  const vXsf _1_mass = floatX(1 / grain.mass);
  const vXsf p[4] = {floatX(dt), floatX(dt*dt/2), floatX(dt*dt*dt/6), floatX(dt*dt*dt*dt/24)};
  const vXsf c[5] = {floatX(19./90), floatX(3/(4*dt)), floatX(2/(dt*dt)), floatX(6/(2*dt*dt*dt)), floatX(24/(12*dt*dt*dt*dt))};
  float* const pPDx0 = grain.PDx[0].begin(), *pPDy0 = grain.PDy[0].begin(), *pPDz0 = grain.PDz[0].begin();
  float* const pPDx1 = grain.PDx[1].begin(), *pPDy1 = grain.PDy[1].begin(), *pPDz1 = grain.PDz[1].begin();
  float* const pPDx2 = grain.PDx[2].begin(), *pPDy2 = grain.PDy[2].begin(), *pPDz2 = grain.PDz[2].begin();
#endif
  float* const pAVx = grain.AVx.begin(), *pAVy = grain.AVy.begin(), *pAVz = grain.AVz.begin();
  float* const pRx = grain.Rx.begin(), *pRy = grain.Ry.begin(), *pRz = grain.Rz.begin(),
    *pRw = grain.Rw.begin();
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   const vXsf Fx = load(pFx, i), Fy = load(pFy, i), Fz = load(pFz, i);
   vXsf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
   vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
#if EULER
   // Symplectic Euler
   Vx += dt_mass * Fx;
   Vy += dt_mass * Fy;
   Vz += dt_mass * Fz;
   Px += dt * Vx;
   Py += dt * Vy;
   Pz += dt * Vz;
#elif NEWMARK
   const vXsf Fx0 = load(pFx0, i), Fy0 = load(pFy0, i), Fz0 = load(pFz0, i);
   Px += dt * Vx + dt2_4m * (Fx0 + Fx);
   Py += dt * Vy + dt2_4m * (Fy0 + Fy);
   Pz += dt * Vz + dt2_4m * (Fz0 + Fz);
   Vx += dt_2mass * (Fx0 + Fx);
   Vy += dt_2mass * (Fy0 + Fy);
   Vz += dt_2mass * (Fz0 + Fz);
#else
   // 5th order "Gear" explicit backward differentiation formula predictor corrector linear multistep method
   vXsf PDx0 = load(pPDx0, i), PDy0 = load(pPDy0, i), PDz0 = load(pPDz0, i);
   vXsf PDx1 = load(pPDx1, i), PDy1 = load(pPDy1, i), PDz1 = load(pPDz1, i);
   vXsf PDx2 = load(pPDx2, i), PDy2 = load(pPDy2, i), PDz2 = load(pPDz2, i);

   const vXsf Rx = p[1] * (Fx * _1_mass - PDx0);
   const vXsf Ry = p[1] * (Fy * _1_mass - PDy0);
   const vXsf Rz = p[1] * (Fz * _1_mass - PDz0);
   // "Correction"
   Px += c[0]*Rx;
   Py += c[0]*Ry;
   Pz += c[0]*Rz;

   Vx += c[1]*Rx;
   Vy+= c[1]*Ry;
   Vz += c[1]*Rz;

   PDx0 += c[2]*Rx;
   PDy0 += c[2]*Ry;
   PDz0 += c[2]*Rz;

   PDx1 += c[3]*Rx;
   PDy1 += c[3]*Ry;
   PDz1 += c[3]*Rz;

   PDx2 += c[4]*Rx;
   PDy2 += c[4]*Ry;
   PDz2 += c[4]*Rz;

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

   Px += p[3]*PDx2;
   Py += p[3]*PDy2;
   Pz += p[3]*PDz2;

   Vx += p[0]*PDx0;
   Vy += p[0]*PDy0;
   Vz += p[0]*PDz0;

   Vx += p[1]*PDx1;
   Vy += p[1]*PDy1;
   Vz += p[1]*PDz1;

   Vx += p[2]*PDx2;
   Vy += p[2]*PDy2;
   Vz += p[2]*PDz2;

   PDx0 += p[0]*PDx1;
   PDy0 += p[0]*PDy1;
   PDz0 += p[0]*PDz1;

   PDx0 += p[1]*PDx2;
   PDy0 += p[1]*PDy2;
   PDz0 += p[1]*PDz2;

   PDx1 += p[0]*PDx2;
   PDy1 += p[0]*PDy2;
   PDz1 += p[0]*PDz2;

   store(pPDx0, i, PDx0); store(pPDy0, i, PDy0); store(pPDz0, i, PDz0);
   store(pPDx1, i, PDx1); store(pPDy1, i, PDy1); store(pPDz1, i, PDz1);
   store(pPDx2, i, PDx2); store(pPDy2, i, PDy2); store(pPDz2, i, PDz2);
#endif
   //Pz = min(topZ, Pz);
   store(pVx, i, Vx); store(pVy, i, Vy); store(pVz, i, Vz);
   store(pPx, i, Px); store(pPy, i, Py); store(pPz, i, Pz);

   maxGrainVX = max(maxGrainVX, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));

   {
    const vXsf AVx = load(pAVx, i), AVy = load(pAVy, i), AVz = load(pAVz, i);
    vXsf Rx = load(pRx, i), Ry = load(pRy, i), Rz = load(pRz, i), Rw = load(pRw, i);
    const vXsf dRx = dt_2 * (Rw * Rx + AVy*Rz - Ry*AVz);
    const vXsf dRy = dt_2 * (Rw * Ry + AVz*Rx - Rz*AVx);
    const vXsf dRz = dt_2 * (Rw * Rz + AVx*Ry - Rx*AVy);
    const vXsf dRw = dt_2 * -(AVx*Rx + AVy*Ry + AVz*Rz);
    Rx += dRx;
    Ry += dRy;
    Rz += dRz;
    Rw += dRw;
    store(pAVx, i, AVx + dt_angularMass * load(pTx, i));
    store(pAVy, i, AVy + dt_angularMass * load(pTy, i));
    store(pAVz, i, AVz + dt_angularMass * load(pTz, i));
    vXsf normalize = rsqrt(Rx*Rx+Ry*Ry+Rz*Rz+Rw*Rw);
    store(pRx, i, normalize*Rx);
    store(pRy, i, normalize*Ry);
    store(pRz, i, normalize*Rz);
    store(pRw, i, normalize*Rw);
   }
  }
  maxGrainV_[id] = max(maxGrainV_[id], max(maxGrainVX));
 }, threadCount);
 float maxGrainV = 0;
 for(size_t k: range(threadCount)) maxGrainV = ::max(maxGrainV, maxGrainV_[k]);
 this->maxGrainV = maxGrainV;
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * this->dt;
}
