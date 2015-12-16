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
  for(int k: range(grain.count-i)) base[index[k]] = a[k];
 }

 validGrainLattice = true;
}

void Simulation::stepGrainIntegration() {
 if(!grain.count) return;
 for(size_t i: range(grain.count, align(simd, grain.count))) { grain.Fx[i] = 0; grain.Fy[i] = 0; grain.Fz[i] = 0; }
 const/*expr*/ size_t threadCount = ::threadCount();
 float maxGrainV_[threadCount]; mref<float>(maxGrainV_, threadCount).clear(0);
 grainIntegrationTime += parallel_chunk(align(simd, grain.count)/simd, [this, &maxGrainV_](uint id, size_t start, size_t size) {
   const vXsf dt_mass = floatX(dt / grain.mass), dt = floatX(this->dt);
   const float dt_angularMass = this->dt / Grain::angularMass;
   vXsf maxGrainVX = _0f;
   const float* Fx = grain.Fx.data, *Fy = grain.Fy.data, *Fz = grain.Fz.data;
   const float* Tx = grain.Tx.begin(), *Ty = grain.Ty.begin(), *Tz = grain.Tz.begin();
   float* const pVx = grain.Vx.begin(), *pVy = grain.Vy.begin(), *pVz = grain.Vz.begin();
   float* const Px = grain.Px.begin()+simd, *Py = grain.Py.begin()+simd, *Pz = grain.Pz.begin()+simd;
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
float* const Px = grain.Px.begin()+simd, *Py = grain.Py.begin()+simd, *Pz = grain.Pz.begin()+simd;
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
assert(maxX-minX < 16 && maxY-minY < 16 && maxZ-minZ < 16, "grain",
       maxX-minX, maxY-minY, maxZ-minZ, "\n",
       minX, maxX, minY, maxY, minZ, maxZ, grain.count);
min = vec3(minX, minY, minZ);
max = vec3(maxX, maxY, maxZ);
}
#endif
