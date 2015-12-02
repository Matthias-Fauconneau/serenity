#include "simulation.h"
#include "parallel.h"

void Simulation::stepWire() {
 const v8sf m_Gz = float8(Wire::mass * Gz);
 wireInitializationTime += parallel_chunk(wire.count/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    store(wire.Fx, i, _0f);
    store(wire.Fy, i, _0f);
    store(wire.Fz, i, m_Gz);
   }
 }, 1);
}

void Simulation::stepWireTension() {
 if(wire.count == 0) return;
 wireTensionTime.start();
 for(size_t i=0; i<wire.count-1; i+=simd) {
  v8sf Ax = load(wire.Px, i     ), Ay = load(wire.Py, i     ), Az = load(wire.Pz, i    );
#if DEBUG
  v8sf Bx = loadu(wire.Px, i+1), By = loadu(wire.Py, i+1), Bz = loadu(wire.Pz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  v8sf Bx = load(wire.Px, i+1), By = load(wire.Py, i+1), Bz = load(wire.Pz, i+1);
#endif
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf L = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf x = L - float8(wire.internodeLength);
  v8sf fS = - float8(wire.tensionStiffness) * x;
  v8sf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
  v8sf AVx = load(wire.Vx, i     ), AVy = load(wire.Vy, i     ), AVz = load(wire.Vz, i    );
#if DEBUG
  v8sf BVx = loadu(wire.Vx, i+1), BVy = loadu(wire.Vy, i+1), BVz = loadu(wire.Vz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  v8sf BVx = load(wire.Vx, i+1), BVy = load(wire.Vy, i+1), BVz = load(wire.Vz, i+1);
#endif
  v8sf RVx = AVx - BVx, RVy = AVy - BVy, RVz = AVz - BVz;
  v8sf fB = - float8(wire.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
  v8sf f = fS + fB;
  v8sf FTx = f * Nx;
  v8sf FTy = f * Ny;
  v8sf FTz = f * Nz;
  if(i+simd >= wire.count-1) { // Masks invalid force updates (FIXME: assert peeled)
   v8ui mask = _1i;
   for(size_t k=wire.count-1-i; k<simd; k++) mask[k] = 0; // FIXME
   FTx = ::mask(mask, FTx);
   FTy = ::mask(mask, FTy);
   FTz = ::mask(mask, FTz);
  }
  /*for(size_t k: range(8))
   if(i+k<wire.count-1) assert(isNumber(vec3(FTx[k],FTy[k],FTz[k])), f[k], Nx[k], Ny[k], Nz[k], L[k], Ax[k], Bx[k], i, i+k, coun);*/
  store(wire.Fx, i, load(wire.Fx, i) + FTx);
  store(wire.Fy, i, load(wire.Fy, i) + FTy);
  store(wire.Fz, i, load(wire.Fz, i) + FTz);
  // FIXME: parallel
  storeu(wire.Fx, i+1, loadu(wire.Fx, i+1) - FTx);
  storeu(wire.Fy, i+1, loadu(wire.Fy, i+1) - FTy);
  storeu(wire.Fz, i+1, loadu(wire.Fz, i+1) - FTz);
 }
 wireTensionTime.stop();
}

void Simulation::stepWireBendingResistance() {
 if(!Wire::bendStiffness || wire.count < 2) return;
 wireBendingResistanceTime.start();
 for(size_t i: range(1, wire.count-1)) { // TODO: SIMD
  vec3 A = wire.position(i-1), B = wire.position(i), C = wire.position(i+1);
  vec3 a = C-B, b = B-A;
  vec3 c = cross(a, b);
  float length = ::length(c);
  if(length) {
   float angle = atan(length, dot(a, b));
   float p = wire.bendStiffness * angle;
   vec3 dap = cross(a, c) / (::length(a) * length);
   vec3 dbp = cross(b, c) / (::length(b) * length);
   wire.Fx[i+1] += p * (-dap).x;
   wire.Fy[i+1] += p * (-dap).y;
   wire.Fz[i+1] += p * (-dap).z;
   wire.Fx[i] += p * (dap + dbp).x;
   wire.Fy[i] += p * (dap + dbp).y;
   wire.Fz[i] += p * (dap + dbp).z;
   wire.Fx[i-1] += p * (-dbp).x;
   wire.Fy[i-1] += p * (-dbp).y;
   wire.Fz[i-1] += p * (-dbp).z;
   if(Wire::bendDamping) {
    vec3 A = wire.velocity(i-1), B = wire.velocity(i), C = wire.velocity(i+1);
    vec3 axis = cross(C-B, B-A);
    float length = ::length(axis);
    if(length) {
     float angularVelocity = atan(length, dot(C-B, B-A));
     vec3 f = (Wire::bendDamping * angularVelocity / 2 / length) * cross(axis, C-A);
     wire.Fx[i] += f.x;
     wire.Fy[i] += f.y;
     wire.Fz[i] += f.z;
    }
   }
  }
 }
 wireBendingResistanceTime.stop();
}

void Simulation::stepWireIntegration() {
 if(!wire.count) return;
 float maxWireV_[maxThreadCount] = {};
 wireIntegrationTime +=
 parallel_chunk(wire.count/simd, [this,&maxWireV_](uint id, size_t start, size_t size) {
   const v8sf dt_mass = float8(wire.dt_mass), dt = float8(this->dt);
   v8sf maxWireV8 = _0f;
   const float* Fx = wire.Fx.data, *Fy = wire.Fy.data, *Fz = wire.Fz.data;
   float* const pVx = wire.Vx.begin(), *pVy = wire.Vy.begin(), *pVz = wire.Vz.begin();
   float* const Px = wire.Px.begin(), *Py = wire.Py.begin(), *Pz = wire.Pz.begin();
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    // Symplectic Euler
    v8sf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
    Vx += dt_mass * load(Fx, i);
    Vy += dt_mass * load(Fy, i);
    Vz += dt_mass * load(Fz, i);
    store(pVx, i, Vx);
    store(pVy, i, Vy);
    store(pVz, i, Vz);
    store(Px, i, load(Px, i) + dt * Vx);
    store(Py, i, load(Py, i) + dt * Vy);
    store(Pz, i, load(Pz, i) + dt * Vz);
    maxWireV8 = max(maxWireV8, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
   }
   float maxWireV = 0;
   for(size_t k: range(simd)) maxWireV = ::max(maxWireV, maxWireV8[k]);
   maxWireV_[id] = maxWireV;
 }, 1);
 float maxWireV = 0;
 for(size_t k: range(threadCount)) maxWireV = ::max(maxWireV, maxWireV_[k]);
 float maxGrainWireV = maxGrainV + maxWireV;
 grainWireGlobalMinD -= maxGrainWireV * this->dt;
}
