#include "simulation.h"
#include "grain.h"
#include "parallel.h"
#include "membrane.h"

void Simulation::stepGrain() {
 if(!grain->count) return;
 const vXsf m_Gz = floatX(grain->mass * Gz);
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
 tsc grainLatticeTime; grainLatticeTime.start();
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
#if DEBUG
   if(1) for(int k: range(simd))
    assert_(index[k] >= -(base-lattice.cells.data) && index[k]<int(lattice.base.size),
            "\n#", i+k, "/", grain->count,
            "@", index[k], " ", base-lattice.cells.data, "\n",
            "size", lattice.size, "\n",
            "min", minX[k], minY[k], minZ[k], "\n",
            "X", Ax[k], Ay[k], Az[k], "\n",
            "max", lattice.max, "\n",
            "V", grain->Vx[simd+i+k], grain->Vy[simd+i+k], grain->Vz[simd+i+k], "\n",
            "F", grain->Fx[simd+i+k], grain->Fy[simd+i+k], grain->Fz[simd+i+k], "\n" );
#endif
   ::scatter(base, index, a);
  }
 };
 if(grain->count/simd) /*grainLatticeTime +=*/ parallel_chunk(grain->count/simd, scatter);
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
#if DEBUG
   if(1) for(int k: range(simd))
    assert_(index[k] >= -(base-lattice.cells.data) && index[k]<int(lattice.base.size),
            "\ngrain.cc\n#", i+k, "/", grain->count,
            "@", index[k], " ", base-lattice.cells.data, "\n",
            "size", lattice.size, "\n",
            "min", minX[k], minY[k], minZ[k], "\n",
            "X", Ax[k], Ay[k], Az[k], "\n",
            "max", lattice.max, "\n",
            "V", grain->Vx[simd+i+k], grain->Vy[simd+i+k], grain->Vz[simd+i+k], "\n",
            "F", grain->Fx[simd+i+k], grain->Fy[simd+i+k], grain->Fz[simd+i+k], "\n" );
#endif
  for(int k: range(grain->count-i)) base[extract(index, k)] = extract(a, k);
 }

 validGrainLattice = true;
 this->grainLatticeTime += grainLatticeTime.cycleCount();
}

void Simulation::stepGrainIntegration() {
 if(!grain->count) return;
 for(size_t i: range(grain->count, align(simd, grain->count))) {
  grain->Fx[simd+i] = 0;
  grain->Fy[simd+i] = 0;
  grain->Fz[simd+i] = 0;
 }
 const/*expr*/ size_t threadCount = ::threadCount();
 float maxGrainVT2_[threadCount]; mref<float>(maxGrainVT2_, threadCount).clear(0);
 float* const maxGrainVT2 = maxGrainVT2_;
 grainIntegrationTime += parallel_chunk(align(simd, grain->count)/simd, [this, maxGrainVT2](uint id, size_t start, size_t size) {
#if GEAR
  const vXsf _1_mass = floatX(1 / grain->mass);
  const vXsf p[3] = {floatX(dt), floatX(dt*dt/2), floatX(dt*dt*dt/6)};
  const vXsf c[4] = {floatX(1./6), floatX(5/(6*dt)), floatX(1/(dt*dt)), floatX(1/(3*dt*dt*dt))};
  float* const pPDx0 = grain->PDx[0].begin(), *pPDy0 = grain->PDy[0].begin(), *pPDz0 = grain->PDz[0].begin();
  float* const pPDx1 = grain->PDx[1].begin(), *pPDy1 = grain->PDy[1].begin(), *pPDz1 = grain->PDz[1].begin();
#else
  const vXsf dt_mass = floatX(dt / grain->mass), dt = floatX(this->dt);
#endif
  const vXsf dt_angularMass = floatX(this->dt / grain->angularMass);
  const vXsf grainViscosity = floatX(this->grainViscosity);
  const vXsf _1_dt = floatX(1 / this->dt);
  const vXsf _1_4 = floatX(1./4);
  const vXsf _1_2 = floatX(1./2);
  const vXsf _4dt2 = floatX(4 * sq(this->dt));
  const vXsf _16 = floatX(16);
  const vXsf _dt2 = floatX(sq(this->dt));
  const vXsf _4_dt = floatX(4/this->dt);
  const vXsf angularViscosity = floatX(this->angularViscosity);
  vXsf maxGrainVX2 = _0f;
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
#if 0
   if(triaxial || validation) for(int k: range(simd)) {
    if(!(sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 10000*N &&
         sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 100*m/s &&
         Pz[k] < membrane->height)) { log(
        "sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < ?*N && "
        "sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < ?*m/s && "
        "Pz[k] < membrane->height)\n",
       sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 10000*N,
       sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 100*m/s,
       Pz[k] < membrane->height,
       "I", i+k, grain->count,
       "H", membrane->height,
       "X", Px[k], Py[k], Pz[k],"\n"
       "V", Vx[k], Vy[k], Vz[k], sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k],"\n"
       "F", Fx[k], Fy[k], Fz[k], sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k],"\n"
       /*"//",
       "H", (membrane->height-grain->radius) /m,
       "X",  Px[k] /m, Py[k] /m, Pz[k] /m,
       "V", Vx[k] /(m/s), Vy[k] /(m/s), Vz[k] /(m/s),
       "F", Fx[k] /N, Fy[k] /N, Fz[k] /N*/);
     //highlightGrains.append(i+k);
     fail=true; return;
    }
   }
#endif
   Vx *= grainViscosity;
   Vy *= grainViscosity;
   Vz *= grainViscosity;
#if GEAR
   // 4th order "Gear" BDF approximated using a predictor
   vXsf PDx0 = load(pPDx0, i), PDy0 = load(pPDy0, i), PDz0 = load(pPDz0, i);
   vXsf PDx1 = load(pPDx1, i), PDy1 = load(pPDy1, i), PDz1 = load(pPDz1, i);

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

   store(pPDx0, i, PDx0); store(pPDy0, i, PDy0); store(pPDz0, i, PDz0);
   store(pPDx1, i, PDx1); store(pPDy1, i, PDy1); store(pPDz1, i, PDz1);
#else
   // Symplectic Euler
   Vx += dt_mass * Fx;
   Vy += dt_mass * Fy;
   Vz += dt_mass * Fz;
   Px += dt * Vx;
   Py += dt * Vy;
   Pz += dt * Vz;
#endif
   store(pVx, i, Vx); store(pVy, i, Vy); store(pVz, i, Vz);
   store(pPx, i, Px); store(pPy, i, Py); store(pPz, i, Pz);
   maxGrainVX2 = max(maxGrainVX2, Vx*Vx + Vy*Vy + Vz*Vz);

   {
    const vXsf AVx = load(pAVx, i), AVy = load(pAVy, i), AVz = load(pAVz, i);
    const vXsf Rx = load(pRx, i), Ry = load(pRy, i), Rz = load(pRz, i), Rw = load(pRw, i);
    const vXsf W1x = angularViscosity * (AVx + dt_angularMass * load(pTx, i));
    const vXsf W1y = angularViscosity * (AVy + dt_angularMass * load(pTy, i));
    const vXsf W1z = angularViscosity * (AVz + dt_angularMass * load(pTz, i));
#if 0
    const vXsf Qx = Rx + dt_2 * (Rw * Rx + AVy*Rz - Ry*AVz);
    const vXsf Qy = Ry + dt_2 * (Rw * Ry + AVz*Rx - Rz*AVx);
    const vXsf Qz = Rz + dt_2 * (Rw * Rz + AVx*Ry - Rx*AVy);
    const vXsf Qw = Rw + dt_2 * -(AVx*Rx + AVy*Ry + AVz*Rz);
#else
    // Mid point
    const vXsf Wx = _1_2 * (AVx+W1x);
    const vXsf Wy = _1_2 * (AVy+W1y);
    const vXsf Wz = _1_2 * (AVz+W1z);
    const vXsf Bw = _1_dt * Rw - _1_4 * (Rx*Wx + Ry*Wy + Rz*Wz);
    const vXsf Bx = _1_dt * Rx + _1_4 * (Rw*Wx + Wy*Rz - Ry*Wz);
    const vXsf By = _1_dt * Ry + _1_4 * (Rw*Wy + Wz*Rx - Rz*Wx);
    const vXsf Bz = _1_dt * Rz + _1_4 * (Rw*Wz + Wx*Ry - Rx*Wy);
    const vXsf S = _4dt2 / (_16 + _dt2*(Wx*Wx + Wy*Wy + Wz*Wz));
    const vXsf Qw = S * (_4_dt*Bw     -Wx*Bx     -Wy*By      -Wz*Bz);
    const vXsf Qx =  S * (+Wx*Bw +_4_dt*Bx     -Wz*By     +Wy*Bz);
    const vXsf Qy =  S * (+Wy*Bw    +Wz*Bx +_4_dt*By      -Wx*Bz);
    const vXsf Qz =  S * (+Wz*Bw    -Wy*Bx      +Wx*By +_4_dt*Bz);
#endif
    store(pAVx, i, W1x);
    store(pAVy, i, W1y);
    store(pAVz, i, W1z);
    const vXsf normalize = rsqrt(Qx*Qx+Qy*Qy+Qz*Qz+Qw*Qw);
    store(pRx, i, normalize*Qx);
    store(pRy, i, normalize*Qy);
    store(pRz, i, normalize*Qz);
    store(pRw, i, normalize*Qw);
   }
  }
  maxGrainVT2[id] = max(maxGrainVT2[id], max(maxGrainVX2));
 }, threadCount);
 float maxGrainV2 = 0;
 for(size_t k: range(threadCount)) maxGrainV2 = ::max(maxGrainV2, maxGrainVT2[k]);
 maxGrainV = sqrt(maxGrainV2);
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * this->dt;
}
