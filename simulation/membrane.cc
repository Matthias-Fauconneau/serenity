#include "simulation.h"
#include "parallel.h"

void Simulation::stepMembrane() {
 size_t stride = membrane.stride;
 membraneInitializationTime += parallel_chunk(stride/simd, (membrane.count-stride)/simd,
                  [this](uint, uint start, uint size) {
    float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
    for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     *(vXsf*)(Fx+i) = _0f;
     *(vXsf*)(Fy+i) = _0f;
     *(vXsf*)(Fz+i) = _0f;
    }
  }, 1/*maxThreadCount FIXME*/);

 membraneForceTime += parallel_for(1, membrane.H-1, [this](uint, int index) { const int start = index, size = 1;
 //membraneForceTime += parallel_chunk(1, membrane.H-1, [this](uint, int start, int size) {
 const float* const Px = membrane.Px.data, *Py = membrane.Py.data, *Pz = membrane.Pz.data;
 const float* const Vx = membrane.Vx.data, *Vy = membrane.Vy.data, *Vz = membrane.Vz.data;
 float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();

 int stride = membrane.stride, W = membrane.W, margin=membrane.margin;
 const vXsf unused P = floatX(pressure/(2*3)); // area = length(cross)/2 / 3 vertices
 const vXsf internodeLength = floatX(membrane.internodeLength);
 const vXsf tensionStiffness = floatX(membrane.tensionStiffness);
 const vXsf tensionDamping = floatX(membrane.tensionDamping);

  // Tension from previous row
  {
   const int i = start;
   const int base = margin+i*stride;
   const int e0 = base-stride+i%2;
   const int e1 = base-stride-!(i%2);
   const int e2 = base-1;
   for(int j=0; j<W; j+=simd) {
    int index = base+j;
    const vXsf Ox = *(vXsf*)(Px+index);
    const vXsf Oy = *(vXsf*)(Py+index);
    const vXsf Oz = *(vXsf*)(Pz+index);
    const vXsf VOx = *(vXsf*)(Vx+index);
    const vXsf VOy = *(vXsf*)(Vy+index);
    const vXsf VOz = *(vXsf*)(Vz+index);

    // Tension
#define tension(i) \
 const int e = e##i+j; \
 const vXsf Rx = loadu(Px, e) - Ox; \
 const vXsf Ry = loadu(Py, e) - Oy; \
 const vXsf Rz = loadu(Pz, e) - Oz; \
 const vXsf sqL = Rx*Rx+Ry*Ry+Rz*Rz; \
 const vXsf L = sqrt(sqL); \
 const vXsf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L; \
 const vXsf x = L - internodeLength; \
 const vXsf fS = tensionStiffness * x; \
 const vXsf RVx = loadu(Vx, e) - VOx; \
 const vXsf RVy = loadu(Vy, e) - VOy; \
 const vXsf RVz = loadu(Vz, e) - VOz; \
 const vXsf fB = tensionDamping * (Nx * RVx + Ny * RVy + Nz * RVz); \
 const vXsf f = fS + fB; \
 const vXsf tx = f * Nx; \
 const vXsf ty = f * Ny; \
 const vXsf tz = f * Nz;
    vXsf fx, fy, fz;
    {tension(0) fx = tx; fy = ty; fz = tz;}
    {tension(1) fx += tx; fy += ty; fz += tz;}
    {tension(2)
       const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
       const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
       const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
       fx += tx; fy += ty; fz += tz;
    }
    // TODO: pression
    const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
    const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
    const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
   }
  }

   for(int i=start+1; i<start+size; i++) {
    const int base = margin+i*stride;
    const int e0 = base-stride+i%2;
    const int e1 = base-stride-!(i%2);
    const int e2 = base-1;
    for(int j=0; j<W; j+=simd) {
     int index = base+j;
     const vXsf Ox = load(Px, index);
     const vXsf Oy = load(Py, index);
     const vXsf Oz = load(Pz, index);
     const vXsf VOx = load(Vx, index);
     const vXsf VOy = load(Vy, index);
     const vXsf VOz = load(Vz, index);

     vXsf fx, fy, fz;
     {tension(0)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx = tx; fy = ty; fz = tz;
     }
     {tension(1)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx += tx; fy += ty; fz += tz;
     }
     {tension(2)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx += tx; fy += ty; fz += tz;
     }
     #if 1
     // FIXME: merge loads with tension
#define pressure(a, b) \
      const int ea = e##a+j; \
      const vXsf Xa = loadu(Px, ea) - Ox; \
      const vXsf Ya = loadu(Py, ea) - Oy; \
      const vXsf Za = loadu(Pz, ea) - Oz; \
      const int eb = e##b+j; \
      const vXsf Xb = loadu(Px, eb) - Ox; \
      const vXsf Yb = loadu(Py, eb) - Oy; \
      const vXsf Zb = loadu(Pz, eb) - Oz; \
      const vXsf px = (Ya*Zb - Yb*Za); \
      const vXsf py = (Za*Xb - Zb*Xa); \
      const vXsf pz = (Xa*Yb - Xb*Ya); \
      const vXsf ppx = P * px; \
      const vXsf ppy = P * py; \
      const vXsf ppz = P * pz;
      {
       pressure(0, 1)
       {const vXsf dFx = loadu(Fx, ea) + ppx; storeu(Fx, ea, dFx);
       const vXsf dFy = loadu(Fy, ea) + ppy; storeu(Fy, ea, dFy);
       const vXsf dFz = loadu(Fz, ea) + ppz; storeu(Fz, ea, dFz);}
       {const vXsf dFx = loadu(Fx, eb) + ppx; storeu(Fx, eb, dFx);
       const vXsf dFy = loadu(Fy, eb) + ppy; storeu(Fy, eb, dFy);
       const vXsf dFz = loadu(Fz, eb) + ppz; storeu(Fz, eb, dFz);}
       fx += ppx;
       fy += ppy;
       fz += ppz;
      }
      {
       pressure(1, 2)
       {const vXsf dFx = loadu(Fx, ea) + ppx; storeu(Fx, ea, dFx);
       const vXsf dFy = loadu(Fy, ea) + ppy; storeu(Fy, ea, dFy);
       const vXsf dFz = loadu(Fz, ea) + ppz; storeu(Fz, ea, dFz);}
       {const vXsf dFx = loadu(Fx, eb) + ppx; storeu(Fx, eb, dFx);
       const vXsf dFy = loadu(Fy, eb) + ppy; storeu(Fy, eb, dFy);
       const vXsf dFz = loadu(Fz, eb) + ppz; storeu(Fz, eb, dFz);}
       fx += ppx;
       fy += ppy;
       fz += ppz;
      }
#undef pressure
     #endif
     const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
     const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
     const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
    }
   }

  // Tension from next row
  {
   const int i = start+size-1;
   const int base = margin+i*stride;
   const int e3 = base+stride-!(i%2);
   const int e4 = base+stride+(i%2);
   for(int j=0; j<W; j+=simd) {
    int index = base+j;
    const vXsf Ox = *(vXsf*)(Px+index);
    const vXsf Oy = *(vXsf*)(Py+index);
    const vXsf Oz = *(vXsf*)(Pz+index);
    const vXsf VOx = *(vXsf*)(Vx+index);
    const vXsf VOy = *(vXsf*)(Vy+index);
    const vXsf VOz = *(vXsf*)(Vz+index);

    // Tension
    vXsf fx, fy, fz;
    {tension(3) fx = tx; fy = ty; fz = tz;}
    {tension(4) fx += tx; fy += ty; fz += tz;}
    // TODO: pression
    const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
    const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
    const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
   }
  }
#undef tension
 }, 1/*maxThreadCount FIXME: pressure*/);
}

void Simulation::stepMembraneIntegration() {
 if(!membrane.count) return;
 const size_t threadCount = maxThreadCount;
 float maxMembraneV_[threadCount]; mref<float>(maxMembraneV_, threadCount).clear(0);
 membraneIntegrationTime += parallel_for(1, membrane.H-1, [this, &maxMembraneV_](uint id, uint i) {
  const vXsf dt_mass = floatX(this->dt / membrane.mass), dt = floatX(this->dt);//, topZ = floatX(this->topZ);
  vXsf maxMembraneVX = _0f;
  float* const Fx = membrane.Fx.begin(), *Fy = membrane.Fy.begin(), *Fz = membrane.Fz.begin();
  float* const pVx = membrane.Vx.begin(), *pVy = membrane.Vy.begin(), *pVz = membrane.Vz.begin();
  float* const pPx = membrane.Px.begin(), *pPy = membrane.Py.begin(), *pPz = membrane.Pz.begin();
  const int W = membrane.W, stride = membrane.stride, margin=membrane.margin;
  // Adds force from repeated nodes
  Fx[i*stride+margin+0] += Fx[i*stride+margin+W];
  Fy[i*stride+margin+0] += Fy[i*stride+margin+W];
  Fz[i*stride+margin+0] += Fz[i*stride+margin+W];
  Fx[i*stride+margin+W-1] += Fx[i*stride+margin-1];
  Fy[i*stride+margin+W-1] += Fy[i*stride+margin-1];
  Fz[i*stride+margin+W-1] += Fz[i*stride+margin-1];
  int base = margin+i*stride;
  for(int j=0; j<W; j+=simd) {
   int k = base+j;
   // Symplectic Euler
   const vXsf Vx = load(pVx, k) + dt_mass * load(Fx, k);
   const vXsf Vy = load(pVy, k) + dt_mass * load(Fy, k);
   const vXsf Vz = load(pVz, k) + dt_mass * load(Fz, k);
   store(pVx, k, Vx);
   store(pVy, k, Vy);
   store(pVz, k, Vz);
   const vXsf Px = load(pPx, k) + dt * Vx;
   const vXsf Py = load(pPy, k) + dt * Vy;
   const vXsf Pz = load(pPz, k) + dt * Vz;
   //Pz = min(topZ, Pz);
   store(pPx, k, Px);
   store(pPy, k, Py);
   store(pPz, k, Pz);
   maxMembraneVX = max(maxMembraneVX, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));
  }
  // Copies position back to repeated nodes
  pPx[i*stride+margin-1] = pPx[i*stride+margin+W-1];
  pPy[i*stride+margin-1] = pPy[i*stride+margin+W-1];
  pPz[i*stride+margin-1] = pPz[i*stride+margin+W-1];
  pPx[i*stride+margin+W] = pPx[i*stride+margin+0];
  pPy[i*stride+margin+W] = pPy[i*stride+margin+0];
  pPz[i*stride+margin+W] = pPz[i*stride+margin+0];
  // Copies velocities back to repeated nodes
  pVx[i*stride+margin-1] = pVx[i*stride+margin+W-1];
  pVy[i*stride+margin-1] = pVy[i*stride+margin+W-1];
  pVz[i*stride+margin-1] = pVz[i*stride+margin+W-1];
  pVx[i*stride+margin+W] = pVx[i*stride+margin+0];
  pVy[i*stride+margin+W] = pVy[i*stride+margin+0];
  pVz[i*stride+margin+W] = pVz[i*stride+margin+0];
  maxMembraneV_[id] = max(maxMembraneV_[id], max(maxMembraneVX));
 }, 1/*threadCount*/);
 float maxMembraneV = 0;
 for(int k: range(threadCount)) maxMembraneV = ::max(maxMembraneV, maxMembraneV_[k]);
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}

#if 1
void Simulation::domainMembrane(vec3& min, vec3& max) {
#if 0
  const/*expr*/ size_t threadCount = ::min(membrane.H-2, maxThreadCount); // 60*60*16 ~ 57600
  float minX_[threadCount]; mref<float>(minX_, threadCount).clear(0);
  float minY_[threadCount]; mref<float>(minY_, threadCount).clear(0);
  float minZ_[threadCount]; mref<float>(minZ_, threadCount).clear(0);
  float maxX_[threadCount]; mref<float>(maxX_, threadCount).clear(0);
  float maxY_[threadCount]; mref<float>(maxY_, threadCount).clear(0);
  float maxZ_[threadCount]; mref<float>(maxZ_, threadCount).clear(0);
  //size_t stride = membrane.stride, margin = membrane.margin;
  /*domainTime += parallel_chunk((stride+margin)/simd, (membrane.count-stride-margin)/simd,
                   [this, &minX_, &minY_, &minZ_, &maxX_, &maxY_, &maxZ_](uint id, uint start, uint size) {*/
  domainTime += parallel_for(1, membrane.H-1,
                   [this, &minX_, &minY_, &minZ_, &maxX_, &maxY_, &maxZ_](uint id, uint i) {
   int stride = membrane.stride, W = membrane.W, margin = membrane.margin;
     const float* const Px = membrane.Px.begin(), *Py = membrane.Py.begin(), *Pz = membrane.Pz.begin();
     vXsf minX = _0f, minY = _0f, minZ = _0f, maxX = _0f, maxY = _0f, maxZ = _0f;
     for(int j=0; j<W; j+=simd) { // 49
     //for(uint i=start*simd; i<(start+size)*simd; i+=simd) { // 49
      uint k = margin+i*stride+j;
       vXsf X = load(Px, k), Y = load(Py, k), Z = load(Pz, k);
       minX = ::min(minX, X);
       minY = ::min(minY, Y);
       minZ = ::min(minZ, Z);
       maxX = ::max(maxX, X);
       maxY = ::max(maxY, Y);
       maxZ = ::max(maxZ, Z);
     }
     minX_[id] = ::min(minX);
     minY_[id] = ::min(minY);
     minZ_[id] = ::min(minZ);
     maxX_[id] = ::max(maxX);
     maxY_[id] = ::max(maxY);
     maxZ_[id] = ::max(maxZ);
  }, threadCount);
  float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
  for(size_t k: range(threadCount)) {
   minX = ::min(minX, minX_[k]);
   maxX = ::max(maxX, maxX_[k]);
   minY = ::min(minY, minY_[k]);
   maxY = ::max(maxY, maxY_[k]);
   minZ = ::min(minZ, minZ_[k]);
   maxZ = ::max(maxZ, maxZ_[k]);
  }
 }
#else
 vXsf minX_ = _0f, minY_ = _0f, minZ_ = _0f, maxX_ = _0f, maxY_ = _0f, maxZ_ = _0f;
 int stride = membrane.stride, W = membrane.W, margin = membrane.margin;
 const float* const Px = membrane.Px.begin(), *Py = membrane.Py.begin(), *Pz = membrane.Pz.begin();
 for(int i=1; i<membrane.H-1; i++) {
  for(int j=0; j<W; j+=simd) {
   uint k = margin+i*stride+j;
   vXsf X = load(Px, k), Y = load(Py, k), Z = load(Pz, k);
   minX_ = ::min(minX_, X);
   maxX_ = ::max(maxX_, X);
   minY_ = ::min(minY_, Y);
   maxY_ = ::max(maxY_, Y);
   minZ_ = ::min(minZ_, Z);
   maxZ_ = ::max(maxZ_, Z);
  }
 }
 const float minX = ::min(minX_);
 const float minY = ::min(minY_);
 const float minZ = ::min(minZ_);
 const float maxX = ::max(maxX_);
 const float maxY = ::max(maxY_);
 const float maxZ = ::max(maxZ_);
#endif
 assert_(maxX-minX < 4 && maxY-minY < 4 && maxZ-minZ < 4, "membrane",
         minX, maxX, minY, maxY, minZ, maxZ, "\n",
         maxX-minX, maxY-minY, maxZ-minZ, "\n",
         membrane.margin, membrane.W, membrane.H, membrane.stride);
 min = vec3(minX, minY, minZ);
 max = vec3(maxX, maxY, maxZ);
}
#endif
