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
  }, maxThreadCount);

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
#define edge(i) \
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
    {edge(0) fx = tx; fy = ty; fz = tz;}
    {edge(1) fx += tx; fy += ty; fz += tz;}
    {edge(2)
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
     {edge(0)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx = tx; fy = ty; fz = tz;
     }
     {edge(1)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx += tx; fy += ty; fz += tz;
     }
     {edge(2)
        const vXsf dFx = loadu(Fx, e) - tx; storeu(Fx, e, dFx);
        const vXsf dFy = loadu(Fy, e) - ty; storeu(Fy, e, dFy);
        const vXsf dFz = loadu(Fz, e) - tz; storeu(Fz, e, dFz);
        fx += tx; fy += ty; fz += tz;
     }
     #if 0
     for(int a: range(2)) { // TODO: merge pressure with tension evaluation above
      int b = a+1; // TODO: Assert peeled
      const vXsf px = (Y[a]*Z[b] - Y[b]*Z[a]);
      const vXsf py = (Z[a]*X[b] - Z[b]*X[a]);
      const vXsf pz = (X[a]*Y[b] - X[b]*Y[a]);
      const vXsf ppx = P * px;
      const vXsf ppy = P * py;
      const vXsf ppz = P * pz;
      FX[a] += ppx;
      FY[a] += ppy;
      FZ[a] += ppz;
      FX[b] += ppx;
      FY[b] += ppy;
      FZ[b] += ppz;
      FX[3] += ppx;
      FY[3] += ppy;
      FZ[3] += ppz;
     }
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
    {edge(3) fx = tx; fy = ty; fz = tz;}
    {edge(4) fx += tx; fy += ty; fz += tz;}
    // TODO: pression
    const vXsf dFx = load(Fx, index) + fx; store(Fx, index, dFx);
    const vXsf dFy = load(Fy, index) + fy; store(Fy, index, dFy);
    const vXsf dFz = load(Fz, index) + fz; store(Fz, index, dFz);
   }
  }
 }, maxThreadCount);
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
  maxMembraneV_[id] = max(maxMembraneVX);
 }, threadCount);
 float maxMembraneV = 0;
 for(int k: range(threadCount)) maxMembraneV = ::max(maxMembraneV, maxMembraneV_[k]);
 float maxGrainMembraneV = maxGrainV + maxMembraneV;
 grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
}
