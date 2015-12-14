// TODO: Face contacts
#include "simulation.h"
//#include "grid.h"
#include "lattice.h"
#include "parallel.h"

inline float log2(float x) { return __builtin_log2f(x); }

static inline void evaluateGrainMembrane(const size_t start, const size_t size,
                                     const uint* grainMembraneContact, const size_t unused grainMembraneContactSize,
                                     const uint* grainMembraneA, const uint* grainMembraneB,
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
  const vXui contacts = *(vXui*)(grainMembraneContact+i);
  const vXui A = gather(grainMembraneA, contacts), B = gather(grainMembraneB, contacts);
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

  *(vXsf*)(pFx+i) = NFx + FTx;
  *(vXsf*)(pFy+i) = NFy + FTy;
  *(vXsf*)(pFz+i) = NFz + FTz;
  *(vXsf*)(pTAx+i) = RAy*FTz - RAz*FTy;
  *(vXsf*)(pTAy+i) = RAz*FTx - RAx*FTz;
  *(vXsf*)(pTAz+i) = RAx*FTy - RAy*FTx;
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
 if(!grain.count || !membrane.count) return;

 if(grainMembraneGlobalMinD <= 0)  {
  const float R = membrane.radius*(1+1./2);
  vec3 min = vec3(vec2(-R), -membrane.height/64), max = vec3(vec2(R), membrane.height);

  memoryTime.start();
  Lattice<uint16> lattice(sqrt(3.)/(2*Grain::radius), min, max);
  memoryTime.stop();
  grainMembraneLatticeTime.start();
  for(size_t i: range(grain.count)) {
   lattice.cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i;
  }
  grainMembraneLatticeTime.stop();

  const float verletDistance = 2*Grain::radius/sqrt(3.);
  assert_(verletDistance > Grain::radius + 0);

  const int X = lattice.size.x, Y = lattice.size.y;
  const unused uint16* latticeNeighbours[3*3] = {
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

  swap(oldGrainMembraneA, grainMembraneA);
  swap(oldGrainMembraneB, grainMembraneB);
  swap(oldGrainMembraneLocalAx, grainMembraneLocalAx);
  swap(oldGrainMembraneLocalAy, grainMembraneLocalAy);
  swap(oldGrainMembraneLocalAz, grainMembraneLocalAz);
  swap(oldGrainMembraneLocalBx, grainMembraneLocalBx);
  swap(oldGrainMembraneLocalBy, grainMembraneLocalBy);
  swap(oldGrainMembraneLocalBz, grainMembraneLocalBz);

  static constexpr size_t averageGrainMembraneContactCount = 16;
  const size_t GWcc = align(simd, grain.count * averageGrainMembraneContactCount +1);
  if(GWcc > grainMembraneA.capacity) {\
   memoryTime.start();
   grainMembraneA = buffer<uint>(GWcc, 0);
   grainMembraneB = buffer<uint>(GWcc, 0);
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
  grainMembraneSearchTime += parallel_for(0, membrane.H,
  [this, &lattice, &latticeNeighbours, verletDistance, &contactCount](uint, uint rowIndex) {
    int W = membrane.W;
    int base = membrane.margin+rowIndex*membrane.stride;
    for(int j=0; j<W; j++) {
     uint b = base+j;
     int offset = lattice.index(membrane.Px[b], membrane.Py[b], membrane.Pz[b]);
     for(int n: range(3*3)) for(int i: range(3)) {
      int a = *(latticeNeighbours[n] + offset + i);
      if(!a) continue;
      a--;
      float d = sqrt(sq(grain.Px[a]-membrane.Px[b])
                     + sq(grain.Py[a]-membrane.Py[b])
                     + sq(grain.Pz[a]-membrane.Pz[b])); // TODO: SIMD //FIXME: fails with Ofast?
      if(d <= verletDistance) {
       size_t index = contactCount++;
       assert_(index < grainMembraneA.capacity);
       grainMembraneA[index] = a; // Grain
       grainMembraneB[index] = b; // Membrane
      }
     }
    }
  });
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
   uint a = grainMembraneA[i];
   uint b = grainMembraneB[i];
   for(uint k = 0;; k++) {
    size_t j = grainMembraneIndex+k;
    if(j >= oldGrainMembraneA.size || oldGrainMembraneA[grainMembraneIndex+k] != a) break;
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

  assert_(align(simd, grainMembraneA.size+1) <= grainMembraneA.capacity);
  for(size_t i=grainMembraneA.size; i<align(simd, grainMembraneA.size +1); i++) grainMembraneA.begin()[i] = 0;
  assert_(align(simd, grainMembraneB.size+1) <= grainMembraneB.capacity);
  for(size_t i=grainMembraneB.size; i<align(simd, grainMembraneB.size +1); i++) grainMembraneB.begin()[i] = 0;

  grainMembraneGlobalMinD = /*minD*/verletDistance - (Grain::radius+0);
  if(grainMembraneGlobalMinD < 0) log("grainMembraneGlobalMinD", grainMembraneGlobalMinD);

  /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-membrane", grainMembraneSkipped);*/
  grainMembraneSkipped=0;
 } else grainMembraneSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainMembraneA.size) > grainMembraneContact.capacity) {
  grainMembraneContact = buffer<uint>(align(simd, grainMembraneA.size));
 }
 grainMembraneContact.size = 0;
 grainMembraneFilterTime += parallel_chunk(align(simd, grainMembraneA.size)/simd, [&](uint, size_t start, size_t size) {
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    vXui A = *(vXui*)(grainMembraneA.data+i), B = *(vXui*)(grainMembraneB.data+i);
    vXsf Ax = gather(grain.Px.data, A), Ay = gather(grain.Py.data, A), Az = gather(grain.Pz.data, A);
    vXsf Bx = gather(membrane.Px.data, B), By = gather(membrane.Py.data, B), Bz = gather(membrane.Pz.data, B);
    vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    vXsf depth = floatX(Grain::radius+0) - length;
    for(size_t k: range(simd)) {
     size_t j = i+k;
     if(j == grainMembraneA.size) break /*2*/;
     if(extract(depth, k) >= 0) {
      // Creates a map from packed contact to index into unpacked contact list (indirect reference)
      // Instead of packing (copying) the unpacked list to a packed contact list
      // To keep track of where to write back (unpacked) contact positions (for static friction)
      // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
      grainMembraneContact.appendAtomic( j );
     } else {
      // Resets contact (static friction spring)
      grainMembraneLocalAx[j] = 0;
     }
    }
   }
 });
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
 const float mass = 1/(1/Grain::mass+1/membrane.mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainMembraneEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainMembrane(start, size,
                      grainMembraneContact.data, grainMembraneContact.size,
                      grainMembraneA.data, grainMembraneB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      membrane.Px.data, membrane.Py.data, membrane.Pz.data,
                      floatX(Grain::radius+0), floatX(Grain::radius), floatX(0),
                      grainMembraneLocalAx.begin(), grainMembraneLocalAy.begin(), grainMembraneLocalAz.begin(),
                      grainMembraneLocalBx.begin(), grainMembraneLocalBy.begin(), grainMembraneLocalBz.begin(),
                      floatX(K), floatX(Kb),
                      floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                      floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      membrane.Vx.data, membrane.Vy.data, membrane.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grainMembraneFx.begin(), grainMembraneFy.begin(), grainMembraneFz.begin(),
                      grainMembraneTAx.begin(), grainMembraneTAy.begin(), grainMembraneTAz.begin() );
 });
 grainMembraneContactSizeSum += grainMembraneContact.size;

 grainMembraneSumTime.start();
 for(size_t i = 0; i < grainMembraneContact.size; i++) { // Scalar scatter add
  size_t index = grainMembraneContact[i];
  size_t a = grainMembraneA[index];
  size_t b = grainMembraneB[index];
  grain.Fx[a] += grainMembraneFx[i];
  membrane .Fx[b] -= grainMembraneFx[i];
  grain.Fy[a] += grainMembraneFy[i];
  membrane .Fy[b] -= grainMembraneFy[i];
  grain.Fz[a] += grainMembraneFz[i];
  membrane .Fz[b] -= grainMembraneFz[i];
  grain.Tx[a] += grainMembraneTAx[i];
  grain.Ty[a] += grainMembraneTAy[i];
  grain.Tz[a] += grainMembraneTAz[i];
 }
 grainMembraneSumTime.stop();
}
