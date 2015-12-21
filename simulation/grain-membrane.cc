// TODO: Face contacts
#include "simulation.h"
#include "parallel.h"
#include "grain.h"
#include "membrane.h"

#define MEMBRANE_POINT 1
#if !MEMBRANE_POINT
static inline float distance(float Rx, float Ry, float Rz, float Rx0, float Ry0, float Rz0, float Rx1, float Ry1, float Rz1) {
 // -?
 const float Dx = Ry0*Rz1 - Ry1*Rz0;
 const float Dy = Rz0*Rx1 - Rz1*Rx0;
 const float Dz = Rx0*Ry1 - Rx1*Ry0;
 const float L = sqrt(Dx*Dx + Dy*Dy + Dz*Dz);
 const float Nx = Dx/L;
 const float Ny = Dy/L;
 const float Nz = Dz/L;
 const float Px = Ny*Rz1 - Ry1*Nz;
 const float Py = Nz*Rx1 - Rz1*Nx;
 const float Pz = Nx*Ry1 - Rx1*Ny;
 const float det = Px * Rx0 + Py * Ry0 + Pz * Rz0;
 assert_(det > 0.000001, det);
 float invDet = 1 / det;
 float u = invDet * (Px*Rx + Py*Ry + Pz*Rz);
 if(u < 0) u = 0;
 if(u > 1) u = 1;
 const float Qx = Ry*Rz0 - Ry0*Rz;
 const float Qy = Rz*Rx0 - Rz0*Rx;
 const float Qz = Rx*Ry0 - Rx0*Ry;
 float v = invDet * (Qx*Nx + Qy*Ny + Qz*Nz);
 if(v < 0) v = 0;
 if(v  > 1-u) v = 1 - u;
 float t = invDet * (Qx*Rx1 + Qy*Ry1 + Qz*Rz1);
 return t;
}
#endif

static inline void evaluateGrainMembrane(const size_t start, const size_t size,
                                     const int* grainMembraneContact, const size_t unused grainMembraneContactSize,
                                     const int* grainMembraneA, const int* grainMembraneB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const float* membranePx, const float* membranePy, const float* membranePz,
                                     const vXsf Gr_Wr, const vXsf Gr, const vXsf Wr,
                                     float* const grainMembraneLocalAx, float* const grainMembraneLocalAy, float* const grainMembraneLocalAz,
                                     float* const grainMembraneLocalBx, float* const grainMembraneLocalBy, float* const grainMembraneLocalBz,
                                     const vXsf K, const vXsf Kb,
                                     const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                     const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                     const vXsf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                     const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                     const float* ArotationW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = load(grainMembraneContact, i);
  const vXsi A = gather(grainMembraneA, contacts), B = gather(grainMembraneB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf Bx = gather(membranePx, B), By = gather(membranePy, B), Bz = gather(membranePz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Gr_Wr - length;
  const vXsf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const vXsf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const vXsf RBx = + Wr  * Nx, RBy = + Wr * Ny, RBz = + Wr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Membrane

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - gather(BVx, B);
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - gather(BVy, B);
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - gather(BVz, B);
  // Damping
  const vXsf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  const vXsf Fb = - Kb * sqrt(sqrt(depth)) * normalSpeed ; // Damping
  // Normal force
  const vXsf Fn = Fk + Fb;
  const vXsf NFx = Fn * Nx;
  const vXsf NFy = Fn * Ny;
  const vXsf NFz = Fn * Nz;

  // Dynamic friction
  // Tangent relative velocity
  const vXsf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  const vXsf TRVx = RVx - RVn * Nx;
  const vXsf TRVy = RVy - RVn * Ny;
  const vXsf TRVz = RVz - RVn * Nz;
  const vXsf tangentRelativeSpeed = sqrt(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  const maskX div0 = greaterThan(tangentRelativeSpeed, _0f);
  const vXsf Fd = - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = mask3_fmadd(Fd, TRVx, _0f, div0);
  const vXsf FDy = mask3_fmadd(Fd, TRVy, _0f, div0);
  const vXsf FDz = mask3_fmadd(Fd, TRVz, _0f, div0);

  // Gather static frictions
  const vXsf oldLocalAx = gather(grainMembraneLocalAx, contacts);
  const vXsf oldLocalAy = gather(grainMembraneLocalAy, contacts);
  const vXsf oldLocalAz = gather(grainMembraneLocalAz, contacts);
  const vXsf oldLocalBx = gather(grainMembraneLocalBx, contacts);
  const vXsf oldLocalBy = gather(grainMembraneLocalBy, contacts);
  const vXsf oldLocalBz = gather(grainMembraneLocalBz, contacts);

  const vXsf QAx = gather(ArotationX, A);
  const vXsf QAy = gather(ArotationY, A);
  const vXsf QAz = gather(ArotationZ, A);
  const vXsf QAw = gather(ArotationW, A);
  const vXsf X1 = QAw*RAx + RAy*QAz - QAy*RAz;
  const vXsf Y1 = QAw*RAy + RAz*QAx - QAz*RAx;
  const vXsf Z1 = QAw*RAz + RAx*QAy - QAx*RAy;
  const vXsf W1 = - (RAx * QAx + RAy * QAy + RAz * QAz);
  const vXsf newLocalAx = QAw*X1 - (W1*QAx + QAy*Z1 - Y1*QAz);
  const vXsf newLocalAy = QAw*Y1 - (W1*QAy + QAz*X1 - Z1*QAx);
  const vXsf newLocalAz = QAw*Z1 - (W1*QAz + QAx*Y1 - X1*QAy);

  const vXsf newLocalBx = RBx;
  const vXsf newLocalBy = RBy;
  const vXsf newLocalBz = RBz;

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
  const vXsf localBx = blend(reset, oldLocalBx, newLocalBx);
  const vXsf localBy = blend(reset, oldLocalBy, newLocalBy);
  const vXsf localBz = blend(reset, oldLocalBz, newLocalBz);

  const vXsf X = QAw*localAx - (localAy*QAz - QAy*localAz);
  const vXsf Y = QAw*localAy - (localAz*QAx - QAz*localAx);
  const vXsf Z = QAw*localAz - (localAx*QAy - QAx*localAy);
  const vXsf W = localAx * QAx + localAy * QAy + localAz * QAz;
  const vXsf FRAx = QAw*X + W*QAx + QAy*Z - Y*QAz;
  const vXsf FRAy = QAw*Y + W*QAy + QAz*X - Z*QAx;
  const vXsf FRAz = QAw*Z + W*QAz + QAx*Y - X*QAy;
  const vXsf FRBx = localBx;
  const vXsf FRBy = localBy;
  const vXsf FRBz = localBz;

  const vXsf gAx = Ax + FRAx;
  const vXsf gAy = Ay + FRAy;
  const vXsf gAz = Az + FRAz;
  const vXsf gBx = Bx + FRBx;
  const vXsf gBy = By + FRBy;
  const vXsf gBz = Bz + FRBz;
  const vXsf Dx = gBx - gAx;
  const vXsf Dy = gBy - gAy;
  const vXsf Dz = gBz - gAz;
  const vXsf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // Tangent offset
  const vXsf TOx = Dx - Dn * Nx;
  const vXsf TOy = Dy - Dn * Ny;
  const vXsf TOz = Dz - Dn * Nz;
  const vXsf tangentLength = sqrt(TOx*TOx+TOy*TOy+TOz*TOz);
  const vXsf Ks = staticFrictionStiffness * Fn;
  const vXsf Fs = Ks * tangentLength; // 0.1~1 fN
  // Spring direction
  const vXsf SDx = TOx / tangentLength;
  const vXsf SDy = TOy / tangentLength;
  const vXsf SDz = TOz / tangentLength;
  const maskX hasTangentLength = greaterThan(tangentLength, _0f);
  const vXsf sfFb = staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz);
  const maskX hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
                                              & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const vXsf sfFt = maskSub(Fs, hasTangentLength, sfFb);
  const vXsf FTx = mask3_fmadd(sfFt, SDx, FDx, hasStaticFriction & hasTangentLength);
  const vXsf FTy = mask3_fmadd(sfFt, SDy, FDy, hasStaticFriction & hasTangentLength);
  const vXsf FTz = mask3_fmadd(sfFt, SDz, FDz, hasStaticFriction & hasTangentLength);
  // Resets contacts without static friction
  localAx = blend(hasStaticFriction, _0f, localAx); // FIXME use 1s (NaN) not 0s to flag resets

  store(pFx, i, NFx + FTx);
  store(pFy, i, NFy + FTy);
  store(pFz, i, NFz + FTz);
  store(pTAx, i, RAy*FTz - RAz*FTy);
  store(pTAy, i, RAz*FTx - RAx*FTz);
  store(pTAz, i, RAx*FTy - RAy*FTx);
  // Scatter static frictions
  scatter(grainMembraneLocalAx, contacts, localAx);
  scatter(grainMembraneLocalAy, contacts, localAy);
  scatter(grainMembraneLocalAz, contacts, localAz);
  scatter(grainMembraneLocalBx, contacts, localBx);
  scatter(grainMembraneLocalBy, contacts, localBy);
  scatter(grainMembraneLocalBz, contacts, localBz);
 }
}

void Simulation::stepGrainMembrane() {
 if(!grain->count || !membrane->count) return;
 if(grainMembraneGlobalMinD <= 0)  {
  swap(oldGrainMembraneA, grainMembraneA);
  swap(oldGrainMembraneB, grainMembraneB);
  swap(oldGrainMembraneLocalAx, grainMembraneLocalAx);
  swap(oldGrainMembraneLocalAy, grainMembraneLocalAy);
  swap(oldGrainMembraneLocalAz, grainMembraneLocalAz);
  swap(oldGrainMembraneLocalBx, grainMembraneLocalBx);
  swap(oldGrainMembraneLocalBy, grainMembraneLocalBy);
  swap(oldGrainMembraneLocalBz, grainMembraneLocalBz);

  static constexpr size_t averageGrainMembraneContactCount = 16;
  const size_t GWcc = align(simd, grain->count * averageGrainMembraneContactCount +1);
  if(GWcc > grainMembraneA.capacity) {\
   memoryTime.start();
   grainMembraneA = buffer<int>(GWcc, 0);
   grainMembraneB = buffer<int>(GWcc, 0);
   grainMembraneLocalAx = buffer<float>(GWcc, 0);
   grainMembraneLocalAy = buffer<float>(GWcc, 0);
   grainMembraneLocalAz = buffer<float>(GWcc, 0);
   grainMembraneLocalBx = buffer<float>(GWcc, 0);
   grainMembraneLocalBy = buffer<float>(GWcc, 0);
   grainMembraneLocalBz = buffer<float>(GWcc, 0);
   memoryTime.stop();
  }
  grainMembraneA.size = grainMembraneA.capacity;
  grainMembraneB.size = grainMembraneB.capacity;

  atomic contactCount;
#if MEMBRANE_POINT
  grainLattice();

  const float verletDistance = 2*Grain::radius/sqrt(3.);
  assert(verletDistance > Grain::radius + 0);

  const int X = lattice.size.x, Y = lattice.size.y;
  const int* latticeNeighbours[3*3] = {
   lattice.base.data+(-X*Y-X-1),
   lattice.base.data+(-X*Y-1),
   lattice.base.data+(-X*Y+X-1),

   lattice.base.data+(-X-1),
   lattice.base.data+(-1),
   lattice.base.data+(X-1),

   lattice.base.data+(X*Y-X-1),
   lattice.base.data+(X*Y-1),
   lattice.base.data+(X*Y+X-1)
  };

  auto search = [this, &latticeNeighbours, verletDistance, &contactCount](uint, uint rowIndex) {
   const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
   const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
   int* const gmA = grainMembraneA.begin(), *gmB = grainMembraneB.begin();
   const vXsf scale = floatX(lattice.scale);
   const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
   const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
   const vXsf verletDistanceX = floatX(verletDistance);
   const vXsi _1i = intX(-1);
   int W = membrane->W;
   int base = membrane->margin+rowIndex*membrane->stride;
   for(int j=0; j<W; j+=simd) {
    int i = base+j;
    vXsi b = intX(i)+_seqi;
    const vXsf Bx = load(mPx, i), By = load(mPy, i), Bz = load(mPz, i);
    vXsi index = convert(scale*(Bz-minZ)) * sizeYX
                       + convert(scale*(By-minY)) * sizeX
                       + convert(scale*(Bx-minX));
    // Neighbours
    for(int n: range(3*3)) for(int i: range(3)) {
     vXsi a = gather(latticeNeighbours[n]+i, index);
     const vXsf Ax = gather(gPx, a), Ay = gather(gPy, a), Az = gather(gPz, a);
     const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     vXsf distance = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
     maskX mask = notEqual(a, _1i) & lessThan(distance, verletDistanceX);
     uint targetIndex = contactCount.fetchAdd(countBits(mask));
     compressStore(gmA+targetIndex, mask, a);
     compressStore(gmB+targetIndex, mask, b);
    }
   }
  };
  grainMembraneSearchTime += parallel_for(0, membrane->H, search);
#else // Face
  const float verletDistance = 2*Grain::radius/sqrt(3.); // TODO: Face with Lattice
  assert(verletDistance > Grain::radius + 0);

  atomic contactCount;
  auto search = [&](uint, uint rowIndex) {
   const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
   const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
   int* const gmA = grainMembraneA.begin(), *gmB = grainMembraneB.begin();
   //const vXsf verletDistanceX = floatX(verletDistance);
   const int W = membrane->W;
   const int stride = membrane->stride;
   const int base = membrane->margin+rowIndex*stride;
   const int e0 = base-stride+rowIndex%2;
   const int e1 = base-stride-!(rowIndex%2);
   const int e2 = base-1;
   for(int j=0; j<W; j++) {
    const int index = base+j;
    const float Ox = mPx[index];
    const float Oy = mPy[index];
    const float Oz = mPz[index];
    const int e0j = e0+j;
    const int e1j = e1+j;
    const int e2j = e2+j;
    const float Rx0 = mPx[e0j], Ry0 = mPy[e0j], Rz0 = mPz[e0j];
    const float Rx1 = mPx[e1j], Ry1 = mPy[e1j], Rz1 = mPz[e1j];
    const float Rx2 = mPx[e2j], Ry2 = mPy[e2j], Rz2 = mPz[e2j];
    for(int a: range(grain.count)) {
     const float Ax = gPx[a], Ay = gPy[a], Az = gPz[a];
     const float Rx = Ax-Ox, Ry = Ay-Oy, Az = Az-Oz;
     // (.,0,1)
     float distance = ::distance(Rx, Ry, Rz, Rx0, Ry0, Rz0, Rx1, Ry1, Rz1);
     bool mask = distance < verletDistance;
     if(mask) {
      uint targetIndex = contactCount.fetchAdd(1);
      gmA[targetIndex] = a;
      gmB[targetIndex] = b;
     }
     // (.,1,2)
     float distance = ::distance(Rx, Ry, Rz, Rx1, Ry1, Rz1, Rx2, Ry2, Rz2);
     bool mask = distance < verletDistance;
     if(mask) {
      uint targetIndex = contactCount.fetchAdd(1);
      gmA[targetIndex] = a;
      gmB[targetIndex] = b;
     }
    }
   }
  };
  parallel_for(1, membrane->H, search);
#endif

  if(!contactCount) return;
  grainMembraneA.size = contactCount;
  grainMembraneB.size = contactCount;
  grainMembraneLocalAx.size = contactCount;
  grainMembraneLocalAy.size = contactCount;
  grainMembraneLocalAz.size = contactCount;
  grainMembraneLocalBx.size = contactCount;
  grainMembraneLocalBy.size = contactCount;
  grainMembraneLocalBz.size = contactCount;

  grainMembraneRepackFrictionTime.start();
  size_t grainMembraneIndex = 0; // Index of first contact with A in old grainMembrane[Local]A|B list
  for(uint i=0; i<grainMembraneA.size; i++) { // seq
   int a = grainMembraneA[i];
   int b = grainMembraneB[i];
   for(uint k = 0;; k++) {
    size_t j = grainMembraneIndex+k;
    if(j >= oldGrainMembraneA.size || oldGrainMembraneA[j] != a) break;
    if(oldGrainMembraneB[j] == b) { // Repack existing friction
     grainMembraneLocalAx[i] = oldGrainMembraneLocalAx[j];
     grainMembraneLocalAy[i] = oldGrainMembraneLocalAy[j];
     grainMembraneLocalAz[i] = oldGrainMembraneLocalAz[j];
     grainMembraneLocalBx[i] = oldGrainMembraneLocalBx[j];
     grainMembraneLocalBy[i] = oldGrainMembraneLocalBy[j];
     grainMembraneLocalBz[i] = oldGrainMembraneLocalBz[j];
     goto break_;
    }
   } /*else*/ { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainMembraneLocalAx[i] = 0;
   }
   /**/break_:;
   while(grainMembraneIndex < oldGrainMembraneA.size && oldGrainMembraneA[grainMembraneIndex] == a)
    grainMembraneIndex++;
  }
  grainMembraneRepackFrictionTime.stop();

  assert(align(simd, grainMembraneA.size+1) <= grainMembraneA.capacity);
  for(size_t i=grainMembraneA.size; i<align(simd, grainMembraneA.size +1); i++) grainMembraneA.begin()[i] = -1;
  assert(align(simd, grainMembraneB.size+1) <= grainMembraneB.capacity);
  for(size_t i=grainMembraneB.size; i<align(simd, grainMembraneB.size +1); i++) grainMembraneB.begin()[i] = 0;

  grainMembraneGlobalMinD = /*minD*/verletDistance - (Grain::radius+0);
  if(grainMembraneGlobalMinD < 0) log("grainMembraneGlobalMinD", grainMembraneGlobalMinD);

  /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-membrane", grainMembraneSkipped);*/
  grainMembraneSkipped=0;
 } else grainMembraneSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainMembraneA.size) > grainMembraneContact.capacity) {
  grainMembraneContact = buffer<int>(align(simd, grainMembraneA.size));
 }
 grainMembraneContact.size = 0;

 atomic contactCount;
 auto filter = [&](uint, uint start, uint size) {
  const int* const gmA = grainMembraneA.data, *gmB = grainMembraneB.data;
  const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
  const float* const mPx = membrane->Px.data, *mPy = membrane->Py.data, *mPz = membrane->Pz.data;
  float* const gmL = grainMembraneLocalAx.begin();
  int* const gmContact = grainMembraneContact.begin();
  const vXsf Gr = floatX(Grain::radius);
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
   vXsi A = load(gmA, i), B = load(gmB, i);
   vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
   vXsf Bx = gather(mPx, B), By = gather(mPy, B), Bz = gather(mPz, B);
   vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
   vXsf depth = Gr - length;
   maskX contact = greaterThanOrEqual(depth, _0f);
   maskStore(gmL+i, ~contact, _0f);
   uint index = contactCount.fetchAdd(countBits(contact));
   compressStore(gmContact+index, contact, intX(i)+_seqi);
  }
 };
 if(grainMembraneA.size/simd) grainMembraneFilterTime += parallel_chunk(grainMembraneA.size/simd, filter);
 // The partial iteration has to be executed last so that invalid contacts are trailing
 // and can be easily trimmed
 if(grainMembraneA.size%simd != 0) filter(0, grainMembraneA.size/simd, 1u);
 grainMembraneContact.size = contactCount;
 while(contactCount.count > 0 && grainMembraneContact[contactCount-1] >= (int)grainMembraneA.size)
  contactCount.count--; // Trims trailing invalid contacts
 grainMembraneContact.size = contactCount;
 if(!grainMembraneContact.size) return;
 for(size_t i=grainMembraneContact.size; i<align(simd, grainMembraneContact.size); i++)
  grainMembraneContact.begin()[i] = grainMembraneA.size;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, grainMembraneContact.size); // Grain-Membrane contact count
 if(GWcc > grainMembraneFx.capacity) {
  memoryTime.start();
  grainMembraneFx = buffer<float>(GWcc);
  grainMembraneFy = buffer<float>(GWcc);
  grainMembraneFz = buffer<float>(GWcc);
  grainMembraneTAx = buffer<float>(GWcc);
  grainMembraneTAy = buffer<float>(GWcc);
  grainMembraneTAz = buffer<float>(GWcc);
  memoryTime.stop();
 }
 grainMembraneFx.size = GWcc;
 grainMembraneFy.size = GWcc;
 grainMembraneFz.size = GWcc;
 grainMembraneTAx.size = GWcc;
 grainMembraneTAy.size = GWcc;
 grainMembraneTAz.size = GWcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Grain::poissonRatio))/Grain::elasticModulus);
 constexpr float R = 1/(Grain::curvature+Membrane::curvature);
 const float K = 4./3*E*sqrt(R);
 const float mass = 1/(1/Grain::mass+1/membrane->mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainMembraneEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainMembrane(start, size,
                      grainMembraneContact.data, grainMembraneContact.size,
                      grainMembraneA.data, grainMembraneB.data,
                      grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd,
                      membrane->Px.data, membrane->Py.data, membrane->Pz.data,
                      floatX(Grain::radius+0), floatX(Grain::radius), floatX(0),
                      grainMembraneLocalAx.begin(), grainMembraneLocalAy.begin(), grainMembraneLocalAz.begin(),
                      grainMembraneLocalBx.begin(), grainMembraneLocalBy.begin(), grainMembraneLocalBz.begin(),
                      floatX(K), floatX(Kb),
                      floatX(staticFrictionStiffness), floatX(dynamicGrainMembraneFrictionCoefficient),
                      floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                      grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd,
                      membrane->Vx.data, membrane->Vy.data, membrane->Vz.data,
                      grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd,
                      grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd,
                      grainMembraneFx.begin(), grainMembraneFy.begin(), grainMembraneFz.begin(),
                      grainMembraneTAx.begin(), grainMembraneTAy.begin(), grainMembraneTAz.begin() );
 });
 grainMembraneContactSizeSum += grainMembraneContact.size;

 grainMembraneSumTime.start();
#if RADIAL
 float radialForce = 0;
#endif
 for(size_t i = 0; i < grainMembraneContact.size; i++) { // Scalar scatter add
  size_t index = grainMembraneContact[i];
  size_t a = grainMembraneA[index];
  size_t b = grainMembraneB[index];
  grain->Fx[simd+a] += grainMembraneFx[i];
  membrane->Fx[b] -= grainMembraneFx[i];
  grain->Fy[simd+a] += grainMembraneFy[i];
  membrane->Fy[b] -= grainMembraneFy[i];
  grain->Fz[simd+a] += grainMembraneFz[i];
  membrane->Fz[b] -= grainMembraneFz[i];
  grain->Tx[simd+a] += grainMembraneTAx[i];
  grain->Ty[simd+a] += grainMembraneTAy[i];
  grain->Tz[simd+a] += grainMembraneTAz[i];
#if RADIAL
  vec2 N = grain->position(a).xy();
  N /= length(N);
  radialForce += dot(N, vec2(grainMembraneFx[i], grainMembraneFy[i]));
#endif
 }
#if RADIAL
 this->radialForce += -radialForce;
 radialSumStepCount++;
#endif
 grainMembraneSumTime.stop();
}
