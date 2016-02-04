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
#if 0
   if(1) for(int k: range(simd))
    assert_(index[k] >= -(base-lattice.cells.data) && index[k]<int(lattice.base.size),
            "#", i+k, "/", grain->count,
            "@", index[k], "<", base-lattice.cells.data,
            "size", lattice.size,
            "min", minX[k], minY[k], minZ[k],
            "X", Ax[k], Ay[k], Az[k],
            "V", grain->Vx[simd+i+k], grain->Vy[simd+i+k], grain->Vz[simd+i+k],
      "F", grain->Fx[simd+i+k], grain->Fy[simd+i+k], grain->Fz[simd+i+k],
      "//",
      "X", Ax[k] /m, Ay[k] /m, Az[k] /m,
      "V", grain->Vx[simd+i+k] /(m/s), grain->Vy[simd+i+k] /(m/s), grain->Vz[simd+i+k] /(m/s),
      "F", grain->Fx[simd+i+k] /N, grain->Fy[simd+i+k] /N, grain->Fz[simd+i+k] /N
      );
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
  for(int k: range(grain->count-i)) base[extract(index, k)] = extract(a, k);
 }

 validGrainLattice = true;
 this->grainLatticeTime += grainLatticeTime.cycleCount();
}

#define PCDM 0
#define MPQ 1
#if PCDM
static inline void qmul(vXsf Qx, vXsf Qy, vXsf Qz, vXsf Qw, vXsf Vx, vXsf Vy, vXsf Vz, vXsf Vw,
                          vXsf& QVx, vXsf& QVy, vXsf& QVz, vXsf& QVw) {
 // Qw*Vv + Vw*Qv + Qv×Vv, Qw*Vw - Qv·Vv
 QVx  = Qw*Vx + Qx*Vw + Qy*Vz - Vy*Qz;
 QVy  = Qw*Vy + Qy*Vw + Qz*Vx - Vz*Qx;
 QVz  = Qw*Vz + Qz*Vw + Qx*Vy - Vx*Qy;
 QVw = Qw*Vw - Vx*Qx - Vy*Qy - Vz*Qz;
}
static inline v8sf cos(v8sf x) { return {cos(x[0]),cos(x[1]),cos(x[2]),cos(x[3]),
                                                           cos(x[4]),cos(x[5]),cos(x[6]),cos(x[7])}; }
static inline v8sf sin(v8sf x) { return {sin(x[0]),sin(x[1]),sin(x[2]),sin(x[3]),
                                                           sin(x[4]),sin(x[5]),sin(x[6]),sin(x[7])}; }
#endif

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
#if PCDM
  const vXsf _1_2 = floatX(1./2);
  const vXsf _1_4 = floatX(1./4);
  const vXsf dt_4 = floatX(this->dt / 4);
#elif MPQ
  const vXsf _1_dt = floatX(1 / this->dt);
  const vXsf _1_4 = floatX(1./4);
  const vXsf _1_2 = floatX(1./2);
  const vXsf _4dt2 = floatX(4 * sq(this->dt));
  const vXsf _16 = floatX(16);
  const vXsf _dt2 = floatX(sq(this->dt));
  const vXsf _4_dt = floatX(4/this->dt);
#else
  const vXsf dt_2 = floatX(this->dt / 2);
  const vXsf angularViscosity = floatX(this->angularViscosity);
#endif
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
   for(int k: range(simd)) {
    if(!(sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 20*N &&
         sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 10*m/s &&
         Pz[k] < membrane->height)) { log(
        "sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 100000*N &&\
        sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 100*m/s &&\
        Pz[k] < membrane->height)",
       sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k] < 100*N,
       sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k] < 100*m/s,
       Pz[k] < membrane->height,
       "I", i+k, grain->count,
       "H", membrane->height,
       "X", Px[k], Py[k], Pz[k],
       "V", Vx[k], Vy[k], Vz[k], sqrt(Vx*Vx + Vy*Vy + Vz*Vz)[k],
       "F", Fx[k], Fy[k], Fz[k], sqrt(Fx*Fx + Fy*Fy + Fz*Fz)[k],
       "//",
       "H", (membrane->height-grain->radius) /m,
       "X",  Px[k] /m, Py[k] /m, Pz[k] /m,
       "V", Vx[k] /(m/s), Vy[k] /(m/s), Vz[k] /(m/s),
       "F", Fx[k] /N, Fy[k] /N, Fz[k] /N);
     highlightGrains.append(i+k);
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
   store(pVx, i, Vx); store(pVy, i, Vy); store(pVz, i, Vz);
   store(pPx, i, Px); store(pPy, i, Py); store(pPz, i, Pz);
#endif
   maxGrainVX2 = max(maxGrainVX2, Vx*Vx + Vy*Vy + Vz*Vz);

   {
    vXsf AVx = load(pAVx, i), AVy = load(pAVy, i), AVz = load(pAVz, i);
    vXsf Rx = load(pRx, i), Ry = load(pRy, i), Rz = load(pRz, i), Rw = load(pRw, i);
    //for(int k: range(simd)) if(i+k<grain->count) log("R", Rx[k], Ry[k], Rz[k], Rw[k], "AV", AVx[k], AVy[k], AVz[k]);
#if !PCDM && !MPQ // Forward Euler
    /*//R = normalize(R + dt/2 * R * quat{0, w}); ?
    const vXsf dRx = dt_2 * (Rw * Rx + Ry*AVz - AVy*Rz);
    const vXsf dRy = dt_2 * (Rw * Ry + Rz*AVx - AVz*Rx);
    const vXsf dRz = dt_2 * (Rw * Rz + Rx*AVy - AVx*Ry);
    const vXsf dRw = dt_2 * -(AVx*Rx + Ry*AVy + Rz*AVz);*/
    //R = normalize(R + dt/2 * quat{0, w} * R); !
    const vXsf dRx = dt_2 * (Rw * Rx + AVy*Rz - Ry*AVz);
    const vXsf dRy = dt_2 * (Rw * Ry + AVz*Rx - Rz*AVx);
    const vXsf dRz = dt_2 * (Rw * Rz + AVx*Ry - Rx*AVy);
    const vXsf dRw = dt_2 * -(AVx*Rx + AVy*Ry + AVz*Rz);
    Rx += dRx;
    Ry += dRy;
    Rz += dRz;
    Rw += dRw;
    AVx += dt_angularMass * load(pTx, i);
    AVy += dt_angularMass * load(pTy, i);
    AVz += dt_angularMass * load(pTz, i);
    AVx *= angularViscosity;
    AVy *= angularViscosity;
    AVz *= angularViscosity;
    store(pAVx, i, AVx);
    store(pAVy, i, AVy);
    store(pAVz, i, AVz);
    vXsf normalize = rsqrt(Rx*Rx+Ry*Ry+Rz*Rz+Rw*Rw);
    store(pRx, i, normalize*Rx);
    store(pRy, i, normalize*Ry);
    store(pRz, i, normalize*Rz);
    store(pRw, i, normalize*Rw);
#elif PCDM // PCDM rotation integration
    vXsf Wx, Wy, Wz; qapply(-Rx, -Ry, -Rz, Rw, AVx, AVy, AVz, Wx, Wy, Wz); // R* AV
    vXsf Tx, Ty, Tz; qapply(-Rx, -Ry, -Rz, Rw, load(pTx, i), load(pTy, i), load(pTz, i), Tx, Ty, Tz); // R* T
    vXsf DWx = dt_angularMass * Tx, DWy = dt_angularMass * Ty, DWz = dt_angularMass * Tz;
    vXsf W4x, W4y, W4z; // q (w + 1/4*dw)
    qapply(Rx, Ry, Rz, Rw, Wx + _4*DWx, Wy + _4*DWy, Wz + _4*DWz, W4x, W4y, W4z);
    // qp
    vXsf L = sqrt(W4x*W4x + W4y*W4y + W4z*W4z);
    vXsf A = dt_4*L;
    vXsf S = (v8sf)(greaterThan(L, _0f) & (v8si)(sin(A)/L)); // L ? sin(A)/L : 0
    vXsf Qx, Qy, Qz, Qw;
    qmul(S*W4x, S*W4y, S*W4z, cos(A), Rx, Ry, Rz, Rw, Qx, Qy, Qz, Qw);
    // qp * (w + 1/2*dw)
    vXsf W2x, W2y, W2z;
    qapply(Qx, Qy, Qz, Qw, Wx + _2*DWx, Wy + _2*DWy, Wz + _2*DWz, W2x, W2y, W2z);
    // Correction (multiplicative update)
    // q = angleVector(dt, w2w) * q;
    {vXsf L = sqrt(W2x*W2x + W2y*W2y + W2z*W2z);
     vXsf A = dt_2*L;
     vXsf S = (v8sf)(greaterThan(L, _0f) & (v8si)(sin(A)/L)); // L ? sin(A)/L : 0
     vXsf Qx, Qy, Qz, Qw;
     qmul(S*W2x, S*W2y, S*W2z, cos(A), Rx, Ry, Rz, Rw, Qx, Qy, Qz, Qw);
     const vXsf normalize = rsqrt(Qx*Qx+Qy*Qy+Qz*Qz+Qw*Qw); // FIXME
     //const vXsf normalize = _1f;
     store(pRx, i, normalize*Qx);
     store(pRy, i, normalize*Qy);
     store(pRz, i, normalize*Qz);
     store(pRw, i, normalize*Qw);
     // ww = q * (w + dw);
     qapply(Qx, Qy, Qz, Qw, Wx+DWx, Wy+DWy, Wz+DWz, AVx, AVy, AVz);
     store(pAVx, i, AVx);
     store(pAVy, i, AVy);
     store(pAVz, i, AVz);
     //log("Q",Qx, Qy, Qz, Qw, "AV", AVx, AVy, AVz);
     //for(int k: range(simd)) if(i+k<grain->count) log("Q", Qx[k], Qy[k], Qz[k], Qw[k], "AV", AVx[k], AVy[k], AVz[k]);
    }
#else
    const vXsf W1x = AVx + dt_angularMass * load(pTx, i);
    const vXsf W1y = AVy + dt_angularMass * load(pTy, i);
    const vXsf W1z = AVz + dt_angularMass * load(pTz, i);
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
    store(pAVx, i, W1x);
    store(pAVy, i, W1y);
    store(pAVz, i, W1z);
    const vXsf normalize = rsqrt(Qx*Qx+Qy*Qy+Qz*Qz+Qw*Qw);
    store(pRx, i, normalize*Qx);
    store(pRy, i, normalize*Qy);
    store(pRz, i, normalize*Qz);
    store(pRw, i, normalize*Qw);
#endif
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
