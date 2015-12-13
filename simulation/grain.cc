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
 }, 1);
}

void Simulation::stepGrainIntegration() {
 if(!grain.count) return;
 const/*expr*/ size_t threadCount = ::threadCount();
 float maxGrainV_[threadCount]; mref<float>(maxGrainV_, threadCount).clear(0);
 grainIntegrationTime += parallel_chunk(align(simd, grain.count)/simd, [this, &maxGrainV_](uint id, size_t start, size_t size) {
   const vXsf dt_mass = floatX(dt / grain.mass), dt = floatX(this->dt);
   const float dt_angularMass = this->dt / Grain::angularMass;
   vXsf maxGrainVX = _0f;
   const float* Fx = grain.Fx.data, *Fy = grain.Fy.data, *Fz = grain.Fz.data;
   const float* Tx = grain.Tx.begin(), *Ty = grain.Ty.begin(), *Tz = grain.Tz.begin();
   float* const pVx = grain.Vx.begin(), *pVy = grain.Vy.begin(), *pVz = grain.Vz.begin();
   float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
   float* const AVx = grain.AVx.begin(), *AVy = grain.AVy.begin(), *AVz = grain.AVz.begin();
   float* const Rx = grain.Rx.begin(), *Ry = grain.Ry.begin(), *Rz = grain.Rz.begin(),
                    *Rw = grain.Rw.begin();
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
    maxGrainVX = max(maxGrainVX, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));

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
  maxGrainV_[id] = max(maxGrainV_[id], max(maxGrainVX));
 }, threadCount);
 float maxGrainV = 0;
 for(size_t k: range(threadCount)) maxGrainV = ::max(maxGrainV, maxGrainV_[k]);
 this->maxGrainV = maxGrainV;
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * this->dt;
}

#if 0
void Simulation::domainGrain(vec3& min, vec3& max) {
float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
vXsf minX_ = _0f, minY_ = _0f, minZ_ = _0f, maxX_ = _0f, maxY_ = _0f, maxZ_ = _0f;
for(size_t i=0; i<grain.count; i+=simd) {
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
assert_(maxX-minX < 16 && maxY-minY < 16 && maxZ-minZ < 16, "grain",
       maxX-minX, maxY-minY, maxZ-minZ, "\n",
       minX, maxX, minY, maxY, minZ, maxZ, grain.count);
min = vec3(minX, minY, minZ);
max = vec3(maxX, maxY, maxZ);
}
#endif
