#include "simulation.h"
#include "grain.h"
#include "parallel.h"
#include "membrane.h"

void Simulation::stepGrain() {
 const vXsf m_Gz = floatX(Grain::mass * Gz);
 grainTime += parallel_chunk(align(simd, grain->count)/simd, [&](uint, size_t start, size_t size) {
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
  store(grain->Fx.begin()+simd, i, _0f);
  store(grain->Fy.begin()+simd, i, _0f);
  store(grain->Fz.begin()+simd, i, m_Gz);
  store(grain->Tx.begin()+simd, i, _0f);
  store(grain->Ty.begin()+simd, i, _0f);
  store(grain->Tz.begin()+simd, i, _0f);
 }
});
}

void Simulation::grainLattice() {
 if(validGrainLattice) return;
 int* const cells = lattice.cells.begin();
 {int size = lattice.cells.size;
 for(int i=0; i<size; i++) cells[i] = -1;}

 auto scatter = [this](uint, uint start, uint size) {
  const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
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
   for(int k: range(simd)) assert_(index[k] >= -(base-lattice.cells.data) && index[k]<int(lattice.base.size),
                                   k, grain->count, index[k], base-lattice.cells.data,
                                   Ax[k], Ay[k], Az[k],
                                   minX[k], minY[k], minZ[k],
                                   lattice.size
                                   );
   ::scatter(base, index, a);
  }
 };
 if(grain->count/simd) grainLatticeTime += parallel_chunk(grain->count/simd, scatter);
 if(grain->count%simd) {
  const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
  int* const base = lattice.base.begin();
  const vXsf scale = floatX(lattice.scale);
  const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
  const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
  uint i=grain->count/simd*simd;
  vXsi a = intX(i)+_seqi;
  const vXsf Ax = load(gPx, i), Ay = load(gPy, i), Az = load(gPz, i);
  vXsi index = convert(scale*(Az-minZ)) * sizeYX
    + convert(scale*(Ay-minY)) * sizeX
    + convert(scale*(Ax-minX));
  for(int k: range(grain->count-i)) base[extract(index, k)] = extract(a, k);
 }

 validGrainLattice = true;
}

void Simulation::stepGrainIntegration() {
 if(!grain->count) return;
 for(size_t i: range(grain->count, align(simd, grain->count))) { grain->Fx[i] = 0; grain->Fy[i] = 0; grain->Fz[i] = 0; }
 const/*expr*/ size_t threadCount = ::threadCount();
 float maxGrainV_[threadCount]; mref<float>(maxGrainV_, threadCount).clear(0);
 grainIntegrationTime += parallel_chunk(align(simd, grain->count)/simd, [this, &maxGrainV_](uint id, size_t start, size_t size) {
  const vXsf dt_mass = floatX(dt / grain->mass), dt = floatX(this->dt);
  const vXsf dt_2 = floatX(this->dt / 2);
  const vXsf dt_angularMass = floatX(this->dt / Grain::angularMass);
  vXsf maxGrainVX = _0f;
  const float* pFx = grain->Fx.data+simd, *pFy = grain->Fy.data+simd, *pFz = grain->Fz.data+simd;
  const float* pTx = grain->Tx.begin()+simd, *pTy = grain->Ty.begin()+simd, *pTz = grain->Tz.begin()+simd;
  float* const pVx = grain->Vx.begin()+simd, *pVy = grain->Vy.begin()+simd, *pVz = grain->Vz.begin()+simd;
  float* const pPx = grain->Px.begin()+simd, *pPy = grain->Py.begin()+simd, *pPz = grain->Pz.begin()+simd;
  float* const pAVx = grain->AVx.begin()+simd, *pAVy = grain->AVy.begin()+simd, *pAVz = grain->AVz.begin()+simd;
  float* const pRx = grain->Rx.begin()+simd, *pRy = grain->Ry.begin()+simd, *pRz = grain->Rz.begin()+simd,
    *pRw = grain->Rw.begin()+simd;
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   const vXsf Fx = load(pFx, i), Fy = load(pFy, i), Fz = load(pFz, i);
   vXsf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
   vXsf Px = load(pPx, i), Py = load(pPy, i), Pz = load(pPz, i);
   //for(int k: range(simd)) log(k, Pz[k], Vz[k], Fz[k]);
   // Symplectic Euler
   Vx += dt_mass * Fx;
   Vy += dt_mass * Fy;
   Vz += dt_mass * Fz;
   Px += dt * Vx;
   Py += dt * Vy;
   Pz += dt * Vz;
   for(int k: range(simd)) {
    assert_(Fz[k] < 100000 &&
            Vz[k] > -10 && Vz[k] < 10 &&
            Pz[k] < membrane->height, membrane->height, Pz[k], Vz[k], Fz[k]);
   }
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
