// TODO: Verlet
#include "simulation.h"
#include "parallel.h"
#include "wire.h"
#include "plate.h"

//Simulation* simulation; // DEBUG

//FIXME: factorize Wire and Grain - Bottom/Top
void evaluateWireObstacle(const int start, const int size,
                                     const int* wireObstacleContact, //int wireObstacleContactSize,
                                     const int* wireObstacleA, //int wireObstacleASize,
                                     const float* wirePx, const float* wirePy, const float* wirePz,
                                     const vXsf obstacleZ, const vXsf Wr,
                                     float* const wireObstacleLocalAx, float* const wireObstacleLocalAy, float* const wireObstacleLocalAz,
                                     float* const wireObstacleLocalBx, float* const wireObstacleLocalBy, float* const wireObstacleLocalBz,
                                     const vXsf K, const vXsf Kb,
                                     const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                     const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                     const vXsf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     float* const pFx, float* const pFy, float* const pFz) {
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = *(vXsi*)(wireObstacleContact+i);
  const vXsi A = gather(wireObstacleA, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  //for(int k: range(simd)) assert_(A[k] >= 0 && A[k] <= simulation->wire->count, A[k]);
  const vXsf Ax = gather(wirePx, A), Ay = gather(wirePy, A), Az = gather(wirePz, A);
  const vXsf depth = (obstacleZ+Wr) - Az; // Top: Az - (obstacleZ-Gr);
  const vXsf Nx = _0f, Ny = _0f, Nz = _1f;
  const vXsf RAx = - Wr  * Nx, RAy = - Wr * Ny, RAz = - Wr * Nz;
  const vXsf RBx = Ax, RBy = Ay, RBz = Az;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Wire - Obstacle

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf RVx = gather(AVx, A);
  const vXsf RVy = gather(AVy, A);
  const vXsf RVz = gather(AVz, A);
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
  const vXsf oldLocalAx = gather(wireObstacleLocalAx, contacts);
  const vXsf oldLocalAy = gather(wireObstacleLocalAy, contacts);
  const vXsf oldLocalAz = gather(wireObstacleLocalAz, contacts);
  const vXsf oldLocalBx = gather(wireObstacleLocalBx, contacts);
  const vXsf oldLocalBy = gather(wireObstacleLocalBy, contacts);
  const vXsf oldLocalBz = gather(wireObstacleLocalBz, contacts);

  const vXsf newLocalAx = RAx;
  const vXsf newLocalAy = RAy;
  const vXsf newLocalAz = RAz;

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

  const vXsf FRAx = localAx;
  const vXsf FRAy = localAy;
  const vXsf FRAz = localAz;
  const vXsf FRBx = localBx;
  const vXsf FRBy = localBy;
  const vXsf FRBz = localBz;

  const vXsf gAx = Ax + FRAx;
  const vXsf gAy = Ay + FRAy;
  const vXsf gAz = Az + FRAz;
  const vXsf gBx = FRBx;
  const vXsf gBy = FRBy;
  const vXsf gBz = FRBz;
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
  /*for(int k: range(simd)) {
   if(i+k < wireObstacleContactSize)
    assert_(length(vec3(pFx[i+k],pFy[i+k],pFz[i+k])) < 100*N,
           length(vec3(pFx[i+k],pFy[i+k],pFz[i+k]))/N, "WB2",
           i, N,
      NFx[i+k], NFy[i+k], NFz[i+k],
      FTx[i+k], FTy[i+k], FTz[i+k]);
  }*/
  // Scatter static frictions
  scatter(wireObstacleLocalAx, contacts, localAx);
  scatter(wireObstacleLocalAy, contacts, localAy);
  scatter(wireObstacleLocalAz, contacts, localAz);
  scatter(wireObstacleLocalBx, contacts, localBx);
  scatter(wireObstacleLocalBy, contacts, localBy);
  scatter(wireObstacleLocalBz, contacts, localBz);
 }
}

void Simulation::stepWireBottom() {
 {
  tsc wireBottomSearchTime; wireBottomSearchTime.start();
  swap(oldWireBottomA, wireBottomA);
  swap(oldWireBottomLocalAx, wireBottomLocalAx);
  swap(oldWireBottomLocalAy, wireBottomLocalAy);
  swap(oldWireBottomLocalAz, wireBottomLocalAz);
  swap(oldWireBottomLocalBx, wireBottomLocalBx);
  swap(oldWireBottomLocalBy, wireBottomLocalBy);
  swap(oldWireBottomLocalBz, wireBottomLocalBz);

  static constexpr size_t averageWireBottomContactCount = 1;
  size_t WBcc = align(simd, wire->count * averageWireBottomContactCount + 1);
  if(WBcc > wireBottomA.capacity) {
   memoryTime.start();
   wireBottomA = buffer<int>(WBcc, 0);
   wireBottomLocalAx = buffer<float>(WBcc, 0);
   wireBottomLocalAy = buffer<float>(WBcc, 0);
   wireBottomLocalAz = buffer<float>(WBcc, 0);
   wireBottomLocalBx = buffer<float>(WBcc, 0);
   wireBottomLocalBy = buffer<float>(WBcc, 0);
   wireBottomLocalBz = buffer<float>(WBcc, 0);
   memoryTime.stop();
  }
  wireBottomA.size = 0;
  wireBottomLocalAx.size = 0;
  wireBottomLocalAy.size = 0;
  wireBottomLocalAz.size = 0;
  wireBottomLocalBx.size = 0;
  wireBottomLocalBy.size = 0;
  wireBottomLocalBz.size = 0;

  size_t wireBottomI = 0; // Index of first contact with A in old wireBottom[Local]A|B list
  auto search = [&](uint, int start, int size) {
   for(int i=start; i<(start+size); i+=1) { // TODO: SIMD
     if(wire->Pz[i] > wire->radius) continue;
     wireBottomA.append( i ); // Wire
     size_t j = wireBottomI;
     if(wireBottomI < oldWireBottomA.size && oldWireBottomA[wireBottomI] == i) {
      // Repack existing friction
      wireBottomLocalAx.append( oldWireBottomLocalAx[j] );
      wireBottomLocalAy.append( oldWireBottomLocalAy[j] );
      wireBottomLocalAz.append( oldWireBottomLocalAz[j] );
      wireBottomLocalBx.append( oldWireBottomLocalBx[j] );
      wireBottomLocalBy.append( oldWireBottomLocalBy[j] );
      wireBottomLocalBz.append( oldWireBottomLocalBz[j] );
     } else { // New contact
      // Appends zero to reserve slot. Zero flags contacts for evaluation.
      // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
      wireBottomLocalAx.append( 0 );
      wireBottomLocalAy.append( 0 );
      wireBottomLocalAz.append( 0 );
      wireBottomLocalBx.append( 0 );
      wireBottomLocalBy.append( 0 );
      wireBottomLocalBz.append( 0 );
     }
     while(wireBottomI < oldWireBottomA.size && oldWireBottomA[wireBottomI] == i)
      wireBottomI++;
    }
  };
  /*wireBottomSearchTime +=*/ parallel_chunk(wire->count, search, 1 /*FIXME*/);

  assert_(align(simd, wireBottomA.size+1) <= wireBottomA.capacity);
  for(size_t i=wireBottomA.size; i<align(simd, wireBottomA.size+1); i++) wireBottomA.begin()[i] = wire->count;
  this->wireBottomSearchTime += wireBottomSearchTime.cycleCount();
 }
 if(!wireBottomA.size) return;
 tsc wireBottomFilterTime; wireBottomFilterTime.start();
 // TODO: verlet
 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, wireBottomA.size) > wireBottomContact.capacity) {
  wireBottomContact = buffer<int>(align(simd, wireBottomA.size));
 }
 wireBottomContact.size = 0;
 auto filter = [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   vXsi A = *(vXsi*)(wireBottomA.data+i);
   vXsf Az = gather(wire->Pz.data, A);
   for(size_t k: range(simd)) {
    size_t j = i+k;
    if(j == wireBottomA.size) break /*2*/;
    if(extract(Az, k) < bottomZ+wire->radius) {
     // Creates a map from packed contact to index into unpacked contact list (indirect reference)
     // Instead of packing (copying) the unpacked list to a packed contact list
     // To keep track of where to write back (unpacked) contact positions (for static friction)
     // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
     wireBottomContact.appendAtomic( j );
    } else {
     // Resets contact (static friction spring)
     wireBottomLocalAx[j] = 0;
    }
   }
  }
 };
 /*wireBottomFilterTime +=*/ parallel_chunk(align(simd, wireBottomA.size)/simd, filter);
 for(size_t i=wireBottomContact.size; i<align(simd, wireBottomContact.size); i++)
  wireBottomContact.begin()[i] = wireBottomA.size;
 this->wireBottomFilterTime += wireBottomFilterTime.cycleCount();
 if(!wireBottomContact) return;

 // Evaluates forces from (packed) intersections (SoA)
 tsc wireBottomEvaluateTime; wireBottomEvaluateTime.start();
 const size_t WBcc = align(simd, wireBottomContact.size); // Wire-Bottom contact count
 if(WBcc > wireBottomFx.capacity) {
  memoryTime.start();
  wireBottomFx = buffer<float>(WBcc);
  wireBottomFy = buffer<float>(WBcc);
  wireBottomFz = buffer<float>(WBcc);
  memoryTime.stop();
 }
 wireBottomFx.size = WBcc;
 wireBottomFy.size = WBcc;
 wireBottomFz.size = WBcc;
 constexpr float E = 1/((1-sq(Wire::poissonRatio))/Wire::elasticModulus+(1-sq(Plate::poissonRatio))/Plate::elasticModulus);
 const float R = 1/(wire->curvature/*+Plate::curvature*/);
 const float K = 4./3*E*sqrt(R);
 const float mass = 1/(1/wire->mass/*+1/Plate::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 /*wireBottomEvaluateTime +=*/ parallel_chunk(WBcc/simd, [&](uint, size_t start, size_t size) {
   evaluateWireObstacle(start, size,
                     wireBottomContact.data, //wireBottomContact.size,
                     wireBottomA.data, //wireBottomA.size,
                     wire->Px.data, wire->Py.data, wire->Pz.data,
                     floatX(bottomZ), floatX(wire->radius),
                     wireBottomLocalAx.begin(), wireBottomLocalAy.begin(), wireBottomLocalAz.begin(),
                     wireBottomLocalBx.begin(), wireBottomLocalBy.begin(), wireBottomLocalBz.begin(),
                     floatX(K), floatX(Kb),
                     floatX(staticFrictionStiffness), floatX(dynamicWireBottomFrictionCoefficient),
                     floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                     wire->Vx.data, wire->Vy.data, wire->Vz.data,
                     wireBottomFx.begin(), wireBottomFy.begin(), wireBottomFz.begin() );
 });
 this->wireBottomEvaluateTime += wireBottomEvaluateTime.cycleCount();
 wireBottomSumTime.start();
 for(size_t index = 0; index<wireBottomContact.size; index++) { // Scalar scatter add
  size_t i = wireBottomA[index];
  wire->Fx[i] += wireBottomFx[index];
  wire->Fy[i] += wireBottomFy[index];
  wire->Fz[i] += wireBottomFz[index];
 }
 wireBottomSumTime.stop();
}
