#include "simulation.h"
#include "parallel.h"
#include "wire.h"

void Simulation::stepWire() {
 const vXsf m_Gz = floatX(wire->mass * Gz);
 float* const wFx = wire->Fx.begin(), *wFy = wire->Fy.begin(), *wFz = wire->Fz.begin();
 wireInitializationTime += parallel_chunk(align(simd, wire->count)/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    store(wFx, i, _0f);
    store(wFy, i, _0f);
    store(wFz, i, m_Gz);
   }
 });
}

void Simulation::stepWireTension() {
 if(wire->count == 0) return;
 /*if(1) {
  const int count = wire->count;
  for(int i=0; i<count-1; i+=simd) {
   for(size_t k: range(simd)) {
    if(i+k+1 >= (size_t)count) break;
    assert_(length(vec3(wFx[i+1+k],wFy[i+1+k],wFz[i+1+k])) < 100*N);
   }
  }
 }*/
 wireTensionTime.start();
 const float* const wPx = wire->Px.data, *wPy = wire->Py.data, *wPz = wire->Pz.data;
 const float* const wVx = wire->Vx.data, *wVy = wire->Vy.data, *wVz = wire->Vz.data;
 float* const wFx = wire->Fx.begin(), *wFy = wire->Fy.begin(), *wFz = wire->Fz.begin();
 const vXsf internodeLength = floatX(wire->internodeLength);
 const vXsf tensionStiffness = floatX(Wire::tensionStiffness);
 const vXsf tensionDamping = floatX(wire->tensionDamping);
 const int count = wire->count;
 for(int i=0; i<count-1; i+=simd) { // TODO: //
  vXsf Ax = load  (wPx, i     ), Ay = load  (wPy, i    ), Az = load  (wPz, i     );
  vXsf Bx = loadu(wPx, i+1), By = loadu(wPy, i+1), Bz = loadu(wPz, i+1);
  vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  vXsf L = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  vXsf x = L - internodeLength;
  vXsf fS = - tensionStiffness * x;
  vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
  vXsf AVx = load  (wVx, i     ), AVy = load  (wVy, i     ), AVz = load (wVz, i     );
  vXsf BVx = loadu(wVx, i+1), BVy = loadu(wVy, i+1), BVz = loadu(wVz, i+1);
  vXsf RVx = AVx - BVx, RVy = AVy - BVy, RVz = AVz - BVz;
  vXsf fB = - tensionDamping * (Nx * RVx + Ny * RVy + Nz * RVz);
  vXsf f = fS + fB;
  vXsf FTx = f * Nx;
  vXsf FTy = f * Ny;
  vXsf FTz = f * Nz;
  if(i+simd >= count-1) { // Masks invalid force updates (FIXME: assert peeled)
   uint16 mask = (~0) << (simd-(count-1-i));
   FTx = blend(mask, _0f, FTx);
   FTy = blend(mask, _0f, FTy);
   FTz = blend(mask, _0f, FTz);
   //for(size_t k: range(count-1-i, simd)) { FTx[k] = 0; FTy[k] = 0; FTz[k] = 0; }
  }
  store(wFx, i, load(wFx, i) + FTx);
  store(wFy, i, load(wFy, i) + FTy);
  store(wFz, i, load(wFz, i) + FTz);
  /*if(1) for(size_t k: range(simd)) {
   if(i+k >= (size_t)count) break;
   if(!(length(vec3(wFx[i+k],wFy[i+k],wFz[i+k])) < 200*N)) {
    cylinders.append(i+k);
    log("i", i, "k", k, "i+k", i+k, "/", count,
        length(vec3(wFx[i+k],wFy[i+k],wFz[i+k]))/N,"N");
    log("W0", wFx[i+k]/N,wFy[i+k]/N,wFz[i+k]/N);
    log(fS[k]/N, fB[k]/N, f[k]/N);
    fail = true;
    return;
   }
  }*/
  storeu(wFx, i+1, loadu(wFx, i+1) - FTx);
  storeu(wFy, i+1, loadu(wFy, i+1) - FTy);
  storeu(wFz, i+1, loadu(wFz, i+1) - FTz);
  /*if(1) for(size_t k: range(simd)) {
   if(i+k+1 >= (size_t)count) break;
   if(!(length(vec3(wFx[i+1+k],wFy[i+1+k],wFz[i+1+k])) < 100*N) || (i+k+1==255 && wFz[i+k+1] < -15*N)) {
    cylinders.append(i+1+k);
    log(i+k+1, count, "W1\n",
      "F", vec3(wFx[i+1+k]/N,wFy[i+1+k]/N,wFz[i+1+k]/N),
       length(vec3(wFx[i+1+k],wFy[i+1+k],wFz[i+1+k]))/N, "\n",
      "FT", FTx[k], FTy[k], FTz[k], "\n",
      "V", wVx[i+1+k],wFy[i+1+k],wFz[i+1+k],"\n",
      "P", wPx[i+1+k],wPy[i+1+k],wPz[i+1+k]);
    fail = true;
    return;
   }
  }*/
 }
 wireTensionTime.stop();
}

// SIMD atan (w/o AVX512F) (y>0)
static const vXsf c1 = floatX(PI/4), c2 = floatX(3*PI/4);
inline vXsf atan(const vXsf y, const vXsf x) {
return blend(lessThan(x, _0f), c2-c1*(x+y)/(y-x), c1-c1*(x-y)/(x+y));
}

void Simulation::stepWireBendingResistance() {
 if(!wire->bendStiffness || wire->count < 2) return;
 const float* const wPx = wire->Px.data, *wPy = wire->Py.data, *wPz = wire->Pz.data;
 //const float* const wVx = wire->Vx.data, *wVy = wire->Vy.data, *wVz = wire->Vz.data;
 float* const wFx = wire->Fx.begin(), *wFy = wire->Fy.begin(), *wFz = wire->Fz.begin();
 const vXsf K = floatX(wire->bendStiffness/*/2*/);
 //const vXsf bendDamping = floatX(wire->bendDamping/2);
 //return; DEBUG
 wireBendingResistanceTime.start();
 for(int i=1; i<(wire->count-1)/simd*simd; i+=simd) { // FIXME: partial last
  const vXsf Ax = load  (wPx, i-1  ), Ay = load  (wPy, i-1 ), Az = load  (wPz, i-1 );
  const vXsf Bx = loadu(wPx, i     ), By = loadu(wPy, i     ), Bz = loadu(wPz, i     );
  const vXsf Cx = loadu(wPx, i+1), Cy = loadu(wPy, i+1), Cz = loadu(wPz, i+1);
  const vXsf aX = Cx-Bx, aY = Cy-By, aZ = Cz-Bz;
  const vXsf bX = Bx-Ax, bY = By-Ay, bZ = Bz-Az;
#if 0
  const vXsf cX = Ay*Bz - By*Az;
  const vXsf cY = Az*Bx - Bz*Ax;
  const vXsf cZ = Ax*By - Bx*Ay;
  const vXsf L = sqrt(cX*cX + cY*cY + cZ*cZ);
  //const vXsf angle = atan(L, aX*bX + aY*bY + aZ*bZ);*/
  const vXsf d = aX*bX + aY*bY + aZ*bZ;
  const vXsf La = sqrt(aX*aX + aY*aY + aZ*aZ);
  const vXsf Lb = sqrt(bX*bX + bY*bY + bZ*bZ);
  const vXsf angle = /*2**/sqrt(_1f-(d/(La*Lb)));
  const vXsf p = bendStiffness * angle;
  const vXsf LaL = La * L;
  const vXsf dapX = (aY*cZ - cY*aZ) / LaL;
  const vXsf dapY = (aZ*cX - cZ*aX) / LaL;
  const vXsf dapZ = (aX*cY - cX*aY) / LaL;
  const vXsf LbL = Lb * L;
  const vXsf dbpX = (bY*cZ - cY*bZ) / LbL;
  const vXsf dbpY = (bZ*cX - cZ*bX) / LbL;
  const vXsf dbpZ = (bX*cY - cX*bY) / LbL;
  const maskX mask = greaterThan(L, _0f);
  maskStore(wFx+i+1, mask, loadu(wFx, i+1) - p * dapX);
  maskStore(wFy+i+1, mask, loadu(wFy, i+1) - p * dapY);
  maskStore(wFz+i+1, mask, loadu(wFz, i+1) - p * dapZ);
  maskStore(wFx+i, mask, loadu(wFx, i) + p * (dapX+dbpX));
  maskStore(wFy+i, mask, loadu(wFy, i) + p * (dapY+dbpY));
  maskStore(wFz+i, mask, loadu(wFz, i) + p * (dapZ+dbpZ));
  maskStore(wFx+i-1, mask, load(wFx, i-1) - p * dbpX);
  maskStore(wFy+i-1, mask, load(wFy, i-1) - p * dbpY);
  maskStore(wFz+i-1, mask, load(wFz, i-1) - p * dbpZ);
#else
  const vXsf La = sqrt(aX*aX + aY*aY + aZ*aZ);
  const vXsf Lb = sqrt(bX*bX + bY*bY + bZ*bZ);
  const vXsf uAx = aX/La, uAy = aY/La, uAz = aZ/La;
  const vXsf uBx = bX/Lb, uBy = bY/Lb, uBz = bZ/Lb;
  const vXsf fX = K*(uBx-uAx), fY = K*(uBy-uAy), fZ = K*(uBz-uAz);
  storeu(wFx+i+1, loadu(wFx, i+1) + fX);
  storeu(wFy+i+1, loadu(wFy, i+1) + fY);
  storeu(wFz+i+1, loadu(wFz, i+1) + fZ);
  store(wFx+i-1, load(wFx, i-1) - fX);
  store(wFy+i-1, load(wFy, i-1) - fY);
  store(wFz+i-1, load(wFz, i-1) - fZ);
#endif
  /*if(wire->bendDamping)*/ /*{
   const vXsf VAx = load  (wVx, i-1  ), VAy = load  (wVy, i-1 ), VAz = load  (wVz, i-1 );
   const vXsf VBx = loadu(wVx, i     ), VBy = loadu(wVy, i     ), VBz = loadu(wVz, i     );
   const vXsf VCx = loadu(wVx, i+1), VCy = loadu(wVy, i+1), VCz = loadu(wVz, i+1);
   const vXsf VaX = VCx-VBx, VaY = VCy-VBy, VaZ = VCz-VBz;
   const vXsf VbX = VBx-VAx, VbY = VBy-VAy, VbZ = VBz-VAz;
   const vXsf VcX = VAy*VBz - VBy*VAz;
   const vXsf VcY = VAz*VBx - VBz*VAx;
   const vXsf VcZ = VAx*VBy - VBx*VAy;
   const vXsf L = sqrt(VcX*VcX + VcY*VcY + VcZ*VcZ);
   const vXsf angularVelocity = atan(L, VaX*VbX + VaY*VbY + VaZ*VbZ);
   const maskX mask = greaterThan(L, _0f);
   const vXsf f = bendDamping * angularVelocity / L;
   const vXsf VcaX = VCx-VAx, VcaY = VCy-VAy, VcaZ = VCz-VAz;
   maskStore(wFx+i, mask, load(wFx, i) + f * (cY*VcaZ - VcaY*cZ));
   maskStore(wFy+i, mask, load(wFy, i) + f * (cZ*VcaX - VcaZ*cX));
   maskStore(wFz+i, mask, load(wFz, i) + f * (cX*VcaY - VcaX*cY));
  }*/
 }
 wireBendingResistanceTime.stop();
}

void Simulation::stepWireIntegration() {
 if(wire->count <= 1) return;
 float maxWireVT2_[::threadCount()]; mref<float>(maxWireVT2_, ::threadCount()).clear(0);
 float* const maxWireVT2 = maxWireVT2_;
 bool fixLast = processState == Pour;
 if(fixLast) {
  wire->Fx[wire->count-1] = 0;
  wire->Fy[wire->count-1] = 0;
  wire->Fz[wire->count-1] = 0;
  wire->Vx[wire->count-1] = 0;
  wire->Vy[wire->count-1] = 0;
  wire->Vz[wire->count-1] = 0;
 }
 wireIntegrationTime +=
 parallel_chunk(align(simd, wire->count-fixLast)/simd, [this,maxWireVT2,fixLast/*DEBUG*/](uint id, size_t start, size_t size) {
   const vXsf dt_mass = floatX(dt / wire->mass), dt = floatX(this->dt);
   const vXsf wireViscosity = floatX(this->wireViscosity);
   vXsf maxWireVX2 = _0f;
   const float* pFx = wire->Fx.data, *pFy = wire->Fy.data, *pFz = wire->Fz.data;
   float* const pVx = wire->Vx.begin(), *pVy = wire->Vy.begin(), *pVz = wire->Vz.begin();
   float* const pPx = wire->Px.begin(), *pPy = wire->Py.begin(), *pPz = wire->Pz.begin();
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    // Symplectic Euler
    vXsf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
    /*if(1) for(size_t k: range(simd)) {
     if(i+k >= (size_t)wire->count-fixLast) break;
     if(!(length(vec3(pFx[i+k],pFy[i+k],pFz[i+k])) < 300*N)) {
      cylinders.append(i+k);
      log(fixLast, processStates[processState], i+k, wire->count, "I0\n",
        "F", vec3(pFx[i+k]/N,pFy[i+k]/N,pFz[i+k]/N),
        length(vec3(pFx[i+k]/N,pFy[i+k]/N,pFz[i+k]/N)));
      fail = true;
      return;
     }
    }*/
    Vx += dt_mass * load(pFx, i);
    Vy += dt_mass * load(pFy, i);
    Vz += dt_mass * load(pFz, i);
    Vx *= wireViscosity;
    Vy *= wireViscosity;
    Vz *= wireViscosity;
    store(pVx, i, Vx);
    store(pVy, i, Vy);
    store(pVz, i, Vz);
    store(pPx, i, load(pPx, i) + dt * Vx);
    store(pPy, i, load(pPy, i) + dt * Vy);
    store(pPz, i, load(pPz, i) + dt * Vz);
    maxWireVX2 = max(maxWireVX2, Vx*Vx + Vy*Vy + Vz*Vz);
    /*if(1) for(size_t k: range(simd)) {
     if(i+k >= (size_t)wire->count) break;
     if(!(length(vec3(pVx[i+k],pVy[i+k],pVz[i+k])) < 100*m/s)) {
      log(i+k, wire->count, length(vec3(pVx[i+k],pVy[i+k],pVz[i+k]))/(m/s), "m/s",
        "F", length(vec3(pFx[i+k]/N,pFy[i+k]/N,pFz[i+k]/N)), vec3(pFx[i+k]/N,pFy[i+k]/N,pFz[i+k]/N),
        wire->mass * Gz);
      fail = true;
      return;
     }
    }*/
   }
   float maxWireV2 = 0;
   for(size_t k: range(simd)) maxWireV2 = ::max(maxWireV2, extract(maxWireVX2, k));
   maxWireVT2[id] = maxWireV2;
 });
 float maxWireV2 = 0;
 for(size_t k: range(threadCount())) maxWireV2 = ::max(maxWireV2, maxWireVT2[k]);
 float maxGrainWireV = maxGrainV + sqrt(maxWireV2);
 wireGrainGlobalMinD -= maxGrainWireV * this->dt;
}
