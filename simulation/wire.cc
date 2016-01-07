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
 for(size_t i=0; i<wire->count-1; i+=simd) {
  vXsf Ax = load(wire->Px, i     ), Ay = load(wire->Py, i     ), Az = load(wire->Pz, i    );
#if DEBUG
  vXsf Bx = loadu(wire->Px, i+1), By = loadu(wire->Py, i+1), Bz = loadu(wire->Pz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  vXsf Bx = load(wire->Px, i+1), By = load(wire->Py, i+1), Bz = load(wire->Pz, i+1);
#endif
  vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  vXsf L = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  vXsf x = L - floatX(wire->internodeLength);
  vXsf fS = - floatX(wire->tensionStiffness) * x;
  vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
  vXsf AVx = load(wire->Vx, i     ), AVy = load(wire->Vy, i     ), AVz = load(wire->Vz, i    );
#if DEBUG
  vXsf BVx = loadu(wire->Vx, i+1), BVy = loadu(wire->Vy, i+1), BVz = loadu(wire->Vz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  vXsf BVx = load(wire->Vx, i+1), BVy = load(wire->Vy, i+1), BVz = load(wire->Vz, i+1);
#endif
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
  /*for(size_t k: range(X))
   if(i+k<wire->count-1) assert(isNumber(vec3(FTx[k],FTy[k],FTz[k])), f[k], Nx[k], Ny[k], Nz[k], L[k], Ax[k], Bx[k], i, i+k, coun);*/
  store(wire->Fx, i, load(wire->Fx, i) + FTx);
  store(wire->Fy, i, load(wire->Fy, i) + FTy);
  store(wire->Fz, i, load(wire->Fz, i) + FTz);
  // FIXME: parallel
  store/*u*/(wire->Fx, i+1, loadu(wire->Fx, i+1) - FTx);
  store/*u*/(wire->Fy, i+1, loadu(wire->Fy, i+1) - FTy);
  store/*u*/(wire->Fz, i+1, loadu(wire->Fz, i+1) - FTz);
 }
 wireTensionTime.stop();
}

void Simulation::stepWireBendingResistance() {
 if(!Wire::bendStiffness || wire->count < 2) return;
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
   if(Wire::bendDamping) {
    vec3 A = wire->velocity(i-1), B = wire->velocity(i), C = wire->velocity(i+1);
    vec3 axis = cross(C-B, B-A);
    float length = ::length(axis);
    if(length) {
     float angularVelocity = atan(length, dot(C-B, B-A));
     vec3 f = (Wire::bendDamping * angularVelocity / 2 / length) * cross(axis, C-A);
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
 if(!wire->count) return;
 float maxWireV2_[::threadCount()]; mref<float>(maxWireV2_, ::threadCount()).clear(0);
 wireIntegrationTime +=
 parallel_chunk(align(simd, wire->count)/simd, [this,&maxWireV2_](uint id, size_t start, size_t size) {
   const vXsf dt_mass = floatX(dt / wire->mass), dt = floatX(this->dt);
   vXsf maxWireVX2 = _0f;
   const float* Fx = wire->Fx.data, *Fy = wire->Fy.data, *Fz = wire->Fz.data;
   float* const pVx = wire->Vx.begin(), *pVy = wire->Vy.begin(), *pVz = wire->Vz.begin();
   float* const Px = wire->Px.begin(), *Py = wire->Py.begin(), *Pz = wire->Pz.begin();
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    // Symplectic Euler
    vXsf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
    Vx += dt_mass * load(Fx, i);
    Vy += dt_mass * load(Fy, i);
    Vz += dt_mass * load(Fz, i);
    store(pVx, i, Vx);
    store(pVy, i, Vy);
    store(pVz, i, Vz);
    store(Px, i, load(Px, i) + dt * Vx);
    store(Py, i, load(Py, i) + dt * Vy);
    store(Pz, i, load(Pz, i) + dt * Vz);
    maxWireVX2 = max(maxWireVX2, Vx*Vx + Vy*Vy + Vz*Vz);
   }
   float maxWireV2 = 0;
   for(size_t k: range(simd)) maxWireV2 = ::max(maxWireV2, extract(maxWireVX2, k));
   maxWireV2_[id] = maxWireV2;
 });
 float maxWireV2 = 0;
 for(size_t k: range(threadCount())) maxWireV2 = ::max(maxWireV2, maxWireV2_[k]);
 float maxGrainWireV = maxGrainV + sqrt(maxWireV2);
 grainWireGlobalMinD -= maxGrainWireV * this->dt;
}

/*void Simulation::domainWire(vec3& min, vec3& max) {
 float* const Px = wire->Px.begin(), *Py = wire->Py.begin(), *Pz = wire->Pz.begin();
 vXsf minX_ = _0f, minY_ = _0f, minZ_ = _0f, maxX_ = _0f, maxY_ = _0f, maxZ_ = _0f;
 for(size_t i=0; i<wire->count; i+=simd) {
  vXsf X = load(Px, i), Y = load(Py, i), Z = load(Pz, i);
  minX_ = ::min(minX_, X);
  maxX_ = ::max(maxX_, X);
  minY_ = ::min(minY_, Y);
  maxY_ = ::max(maxY_, Y);
  minZ_ = ::min(minZ_, Z);
  maxZ_ = ::max(maxZ_, Z);
 }
 const float minX = ::min(minX_);
 const float minY = ::min(minY_);
 const float minZ = ::min(minZ_);
 const float maxX = ::max(maxX_);
 const float maxY = ::max(maxY_);
 const float maxZ = ::max(maxZ_);
 assert(maxX-minX < 16 && maxY-minY < 16 && maxZ-minZ < 16, "wire");
 min = vec3(minX, minY, minZ);
 max = vec3(maxX, maxY, maxZ);
}*/
