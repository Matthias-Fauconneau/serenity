#include "simulation.h"
#include "parallel.h"

void Simulation::stepGrain() {
 grainTime.start();
 for(size_t i: range(grain.count)) {
  grain.Fx[i] = 0; grain.Fy[i] = 0; grain.Fz[i] = Grain::mass * Gz;
  grain.Tx[i] = 0; grain.Ty[i] = 0; grain.Tz[i] = 0;
 }
 grainTime.stop();
}

void Simulation::stepGrainIntegration() {
 if(!grain.count) return;
 float maxGrainV_[maxThreadCount] = {};
 grainIntegrationTime += parallel_chunk(align(simd, grain.count)/simd, [this, &maxGrainV_](uint id, size_t start, size_t size) {
   const v8sf dt_mass = float8(dt / grain.mass), dt = float8(this->dt);
   const float dt_angularMass = this->dt / Grain::angularMass;
   v8sf maxGrainV8 = _0f;
   const float* Fx = grain.Fx.data, *Fy = grain.Fy.data, *Fz = grain.Fz.data;
   const float* Tx = grain.Tx.begin(), *Ty = grain.Ty.begin(), *Tz = grain.Tz.begin();
   float* const pVx = grain.Vx.begin(), *pVy = grain.Vy.begin(), *pVz = grain.Vz.begin();
   float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
   float* const AVx = grain.AVx.begin(), *AVy = grain.AVy.begin(), *AVz = grain.AVz.begin();
   float* const Rx = grain.Rx.begin(), *Ry = grain.Ry.begin(), *Rz = grain.Rz.begin(),
                    *Rw = grain.Rw.begin();
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
    maxGrainV8 = max(maxGrainV8, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));

    for(size_t k: range(simd)) { // FIXME: SIMD
     size_t j = i+k;
     vec4 dr = this->dt/2 * qmul(vec4(AVx[j],AVy[j],AVz[j], 0), vec4(Rx[j],Ry[j],Rz[j],Rw[j]));
     Rx[j] += dr.x;
     Ry[j] += dr.y;
     Rz[j] += dr.z;
     Rw[j] += dr.w;
     AVx[j] += dt_angularMass * Tx[j];
     AVy[j] += dt_angularMass * Ty[j];
     AVz[j] += dt_angularMass * Tz[j];
     float scale = 1./length(vec4(Rx[j],Ry[j],Rz[j],Rw[j]));
     Rx[j] *= scale;
     Ry[j] *= scale;
     Rz[j] *= scale;
     Rw[j] *= scale;
    }
  }
  float maxGrainV = 0;
  for(size_t k: range(simd)) maxGrainV = ::max(maxGrainV, extract(maxGrainV8, k));
  maxGrainV_[id] = maxGrainV;
 }, 1);
 float maxGrainV = 0;
 for(size_t k: range(threadCount)) maxGrainV = ::max(maxGrainV, maxGrainV_[k]);
 this->maxGrainV = maxGrainV;
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * this->dt;
}
