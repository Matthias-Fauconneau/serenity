#include "simulation.h"
#include "parallel.h"
#include "wire.h"

void Simulation::stepWire() {
 const vXsf m_Gz = floatX(wire->mass * Gz);
 wireInitializationTime += parallel_chunk(align(simd, wire->count)/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    store(wire->Fx, i, _0f);
    store(wire->Fy, i, _0f);
    store(wire->Fz, i, m_Gz);
   }
 });
}

void Simulation::stepWireTension() {
 if(wire->count == 0) return;
 wireTensionTime.start();
 for(int i=0; i<wire->count-1; i+=simd) {
  vXsf Ax = load  (wire->Px, i     ), Ay = load  (wire->Py, i    ), Az = load  (wire->Pz, i     );
  vXsf Bx = loadu(wire->Px, i+1), By = loadu(wire->Py, i+1), Bz = loadu(wire->Pz, i+1);
  vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  vXsf L = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  vXsf x = L - floatX(wire->internodeLength);
  vXsf fS = - floatX(Wire::tensionStiffness) * x;
  vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
  vXsf AVx = load  (wire->Vx, i     ), AVy = load  (wire->Vy, i     ), AVz = load (wire->Vz, i     );
  vXsf BVx = loadu(wire->Vx, i+1), BVy = loadu(wire->Vy, i+1), BVz = loadu(wire->Vz, i+1);
  vXsf RVx = AVx - BVx, RVy = AVy - BVy, RVz = AVz - BVz;
  vXsf fB = - floatX(wire->tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
  vXsf f = fS + fB;
  vXsf FTx = f * Nx;
  vXsf FTy = f * Ny;
  vXsf FTz = f * Nz;
  if(i+simd >= wire->count-1) { // Masks invalid force updates (FIXME: assert peeled)
   uint16 mask = (~0) << (simd-(wire->count-i));
   FTx = blend(mask, _0f, FTx);
   FTy = blend(mask, _0f, FTy);
   FTz = blend(mask, _0f, FTz);
  }
  store(wire->Fx, i, load(wire->Fx, i) + FTx);
  store(wire->Fy, i, load(wire->Fy, i) + FTy);
  store(wire->Fz, i, load(wire->Fz, i) + FTz);
  storeu(wire->Fx, i+1, loadu(wire->Fx, i+1) - FTx);
  storeu(wire->Fy, i+1, loadu(wire->Fy, i+1) - FTy);
  storeu(wire->Fz, i+1, loadu(wire->Fz, i+1) - FTz);
 }
 wireTensionTime.stop();
}

void Simulation::stepWireBendingResistance() {
 if(!wire->bendStiffness || wire->count < 2) return;
 wireBendingResistanceTime.start();
 for(size_t i: range(1, wire->count-1)) { // TODO: SIMD
  vec3 A = wire->position(i-1), B = wire->position(i), C = wire->position(i+1);
  vec3 a = C-B, b = B-A;
  vec3 c = cross(a, b);
  float length = ::length(c);
  if(length) {
   float angle = atan(length, dot(a, b));
   float p = wire->bendStiffness * angle;
   vec3 dap = cross(a, c) / (::length(a) * length);
   vec3 dbp = cross(b, c) / (::length(b) * length);
   wire->Fx[i+1] += p * (-dap).x;
   wire->Fy[i+1] += p * (-dap).y;
   wire->Fz[i+1] += p * (-dap).z;
   wire->Fx[i] += p * (dap + dbp).x;
   wire->Fy[i] += p * (dap + dbp).y;
   wire->Fz[i] += p * (dap + dbp).z;
   wire->Fx[i-1] += p * (-dbp).x;
   wire->Fy[i-1] += p * (-dbp).y;
   wire->Fz[i-1] += p * (-dbp).z;
   if(wire->bendDamping) {
    vec3 A = wire->velocity(i-1), B = wire->velocity(i), C = wire->velocity(i+1);
    vec3 axis = cross(C-B, B-A);
    float length = ::length(axis);
    if(length) {
     float angularVelocity = atan(length, dot(C-B, B-A));
     vec3 f = (wire->bendDamping * angularVelocity / 2 / length) * cross(axis, C-A);
     wire->Fx[i] += f.x;
     wire->Fy[i] += f.y;
     wire->Fz[i] += f.z;
    }
   }
  }
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
 }
 wireIntegrationTime +=
 parallel_chunk(align(simd, wire->count-fixLast)/simd, [this,maxWireVT2](uint id, size_t start, size_t size) {
   const vXsf dt_mass = floatX(dt / wire->mass), dt = floatX(this->dt);
   vXsf maxWireVX2 = _0f;
   const float* pFx = wire->Fx.data, *pFy = wire->Fy.data, *pFz = wire->Fz.data;
   float* const pVx = wire->Vx.begin(), *pVy = wire->Vy.begin(), *pVz = wire->Vz.begin();
   float* const pPx = wire->Px.begin(), *pPy = wire->Py.begin(), *pPz = wire->Pz.begin();
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    // Symplectic Euler
    vXsf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
    if(1) for(size_t k: range(simd)) {
     if(i+k >= (size_t)wire->count) break;
     if(!(length(vec3(pFx[i+k],pFy[i+k],pFz[i+k])) < 10000*N)) {
      cylinders.append(i+k);
      log(length(vec3(pFx[i+k],pFy[i+k],pFz[i+k]))/N, "S");
      fail = true;
      return;
     }
    }
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
    if(1) for(size_t k: range(simd)) {
     if(i+k >= (size_t)wire->count) break;
     if(!(length(vec3(pVx[i+k],pVy[i+k],pVz[i+k])) < 1000*m/s)) {
      log(length(vec3(pVx[i+k],pVy[i+k],pVz[i+k]))/(m/s));
      fail = true;
      return;
     }
    }
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
