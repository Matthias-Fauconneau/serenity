// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

//FIXME: factorize Bottom and Top
static void evaluateGrainObstacle(const size_t start, const size_t size,
                           const uint* grainObstacleContact, const size_t unused grainObstacleContactSize,
                           const uint* grainObstacleA,
                           const float* grainPx, const float* grainPy, const float* grainPz,
                           const vXsf obstacleZ, const vXsf Gr,
                           float* const grainObstacleLocalAx, float* const grainObstacleLocalAy, float* const grainObstacleLocalAz,
                           float* const grainObstacleLocalBx, float* const grainObstacleLocalBy, float* const grainObstacleLocalBz,
                           const vXsf K, const vXsf Kb,
                           const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                           const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                           const vXsf staticFrictionDamping,
                           const float* AVx, const float* AVy, const float* AVz,
                           const float* pAAVx, const float* pAAVy, const float* pAAVz,
                           const float* AQX, const float* AQY, const float* AQZ, const float* AQW,
                           float* const pFx, float* const pFy, float* const pFz,
                           float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXui contacts = *(vXui*)(grainObstacleContact+i);
  const vXui A = gather(grainObstacleA, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf depth = (obstacleZ+Gr) - Az; // Top: Az - (obstacleZ-Gr);
  const vXsf Nx = _0f, Ny = _0f, Nz = _1f;
  const vXsf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const vXsf RBx = Ax, RBy = Ay, RBz = Az;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Obstacle

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy);
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz);
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx);
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
  const uint16 div0 = greaterThan(tangentRelativeSpeed, _0f);
  const vXsf Fd = - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = fma(Fd, div0, TRVx, _0f);
  const vXsf FDy = fma(Fd, div0, TRVy, _0f);
  const vXsf FDz = fma(Fd, div0, TRVz, _0f);

  // Gather static frictions
  const vXsf oldLocalAx = gather(grainObstacleLocalAx, contacts);
  const vXsf oldLocalAy = gather(grainObstacleLocalAy, contacts);
  const vXsf oldLocalAz = gather(grainObstacleLocalAz, contacts);
  const vXsf oldLocalBx = gather(grainObstacleLocalBx, contacts);
  const vXsf oldLocalBy = gather(grainObstacleLocalBy, contacts);
  const vXsf oldLocalBz = gather(grainObstacleLocalBz, contacts);

  const vXsf QAx = gather(AQX, A);
  const vXsf QAy = gather(AQY, A);
  const vXsf QAz = gather(AQZ, A);
  const vXsf QAw = gather(AQW, A);
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

  const uint16 reset = equal(oldLocalAx, _0f);
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
  vXsf Fs = Ks * tangentLength; // 0.1~1 fN
  // Spring direction
  const vXsf SDx = TOx / tangentLength;
  const vXsf SDy = TOy / tangentLength;
  const vXsf SDz = TOz / tangentLength;
  const uint16 hasTangentLength = greaterThan(tangentLength, _0f);
  const vXsf sfFb = staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz);
  const uint16 hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
                                              & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const vXsf sfFt = maskSub(Fs, hasTangentLength, Fs, sfFb);
  const vXsf FTx = fma(sfFt, hasStaticFriction, SDx, FDx);
  const vXsf FTy = fma(sfFt, hasStaticFriction, SDy, FDy);
  const vXsf FTz = fma(sfFt, hasStaticFriction, SDz, FDz);
  // Resets contacts without static friction
  localAx = blend(hasStaticFriction, _0f, localAx); // FIXME use 1s (NaN) not 0s to flag resets

  *(vXsf*)(pFx+i) = NFx + FTx;
  *(vXsf*)(pFy+i) = NFy + FTy;
  *(vXsf*)(pFz+i) = NFz + FTz;
  *(vXsf*)(pTAx+i) = RAy*FTz - RAz*FTy;
  *(vXsf*)(pTAy+i) = RAz*FTx - RAx*FTz;
  *(vXsf*)(pTAz+i) = RAx*FTy - RAy*FTx;
  // Scatter static frictions
  scatter(grainObstacleLocalAx, contacts, localAx);
  scatter(grainObstacleLocalAy, contacts, localAy);
  scatter(grainObstacleLocalAz, contacts, localAz);
  scatter(grainObstacleLocalBx, contacts, localBx);
  scatter(grainObstacleLocalBy, contacts, localBy);
  scatter(grainObstacleLocalBz, contacts, localBz);
 }
}

void Simulation::stepGrainBottom() {
 if(!grain.count) return;
 {
  swap(oldGrainBottomA, grainBottomA);
  swap(oldGrainBottomLocalAx, grainBottomLocalAx);
  swap(oldGrainBottomLocalAy, grainBottomLocalAy);
  swap(oldGrainBottomLocalAz, grainBottomLocalAz);
  swap(oldGrainBottomLocalBx, grainBottomLocalBx);
  swap(oldGrainBottomLocalBy, grainBottomLocalBy);
  swap(oldGrainBottomLocalBz, grainBottomLocalBz);

  static constexpr size_t averageGrainBottomContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainBottomContactCount +1);
  if(GBcc > grainBottomA.capacity) {
   grainBottomA = buffer<uint>(GBcc, 0);
   grainBottomLocalAx = buffer<float>(GBcc, 0);
   grainBottomLocalAy = buffer<float>(GBcc, 0);
   grainBottomLocalAz = buffer<float>(GBcc, 0);
   grainBottomLocalBx = buffer<float>(GBcc, 0);
   grainBottomLocalBy = buffer<float>(GBcc, 0);
   grainBottomLocalBz = buffer<float>(GBcc, 0);
  }
  grainBottomA.size = 0;
  grainBottomLocalAx.size = 0;
  grainBottomLocalAy.size = 0;
  grainBottomLocalAz.size = 0;
  grainBottomLocalBx.size = 0;
  grainBottomLocalBy.size = 0;
  grainBottomLocalBz.size = 0;

  size_t grainBottomI = 0; // Index of first contact with A in old grainBottom[Local]A|B list
  auto search = [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
    float depth = (bottomZ+Grain::radius) - grain.Pz[i];
    if(depth < 0) continue;
    grainBottomA.append( i ); // Grain
    size_t j = grainBottomI;
    if(grainBottomI < oldGrainBottomA.size && oldGrainBottomA[grainBottomI] == i) {
     // Repack existing friction
     grainBottomLocalAx.append( oldGrainBottomLocalAx[j] );
     grainBottomLocalAy.append( oldGrainBottomLocalAy[j] );
     grainBottomLocalAz.append( oldGrainBottomLocalAz[j] );
     grainBottomLocalBx.append( oldGrainBottomLocalBx[j] );
     grainBottomLocalBy.append( oldGrainBottomLocalBy[j] );
     grainBottomLocalBz.append( oldGrainBottomLocalBz[j] );
    } else { // New contact
     // Appends zero to reserve slot. Zero flags contacts for evaluation.
     // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
     grainBottomLocalAx.append( 0 );
     grainBottomLocalAy.append( 0 );
     grainBottomLocalAz.append( 0 );
     grainBottomLocalBx.append( 0 );
     grainBottomLocalBy.append( 0 );
     grainBottomLocalBz.append( 0 );
    }
    while(grainBottomI < oldGrainBottomA.size && oldGrainBottomA[grainBottomI] == i)
     grainBottomI++;
   }
  };
  grainBottomFilterTime += parallel_chunk(grain.count, search, 1);

  for(size_t i=grainBottomA.size; i<align(simd, grainBottomA.size+1); i++)
   grainBottomA.begin()[i] = 0;
 }
 if(!grainBottomA) return;

 // TODO: verlet
 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainBottomA.size) > grainBottomContact.capacity) {
  grainBottomContact = buffer<uint>(align(simd, grainBottomA.size));
 }
 grainBottomContact.size = 0;
 auto filter = [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   vXui A = *(vXui*)(grainBottomA.data+i);
   vXsf Az = gather(grain.Pz.data, A);
   vXsf depth = floatX(bottomZ+Grain::radius) - Az;
   for(size_t k: range(simd)) {
    size_t j = i+k;
    if(j == grainBottomA.size) break /*2*/;
    if(extract(depth, k) > 0) {
     // Creates a map from packed contact to index into unpacked contact list (indirect reference)
     // Instead of packing (copying) the unpacked list to a packed contact list
     // To keep track of where to write back (unpacked) contact positions (for static friction)
     // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
     grainBottomContact.append( j );
    } else {
     // Resets contact (static friction spring)
     grainBottomLocalAx[j] = 0;
    }
   }
  }
 };
 grainBottomFilterTime += parallel_chunk(align(simd, grainBottomA.size)/simd, filter, 1);
 for(size_t i=grainBottomContact.size; i<align(simd, grainBottomContact.size); i++)
  grainBottomContact.begin()[i] = grainBottomA.size;
 if(!grainBottomContact) return;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainBottomContact.size); // Grain-Bottom contact count
 if(GBcc > grainBottomFx.capacity) {
  grainBottomFx = buffer<float>(GBcc);
  grainBottomFy = buffer<float>(GBcc);
  grainBottomFz = buffer<float>(GBcc);
  grainBottomTAx = buffer<float>(GBcc);
  grainBottomTAy = buffer<float>(GBcc);
  grainBottomTAz = buffer<float>(GBcc);
 }
 grainBottomFx.size = GBcc;
 grainBottomFy.size = GBcc;
 grainBottomFz.size = GBcc;
 grainBottomTAx.size = GBcc;
 grainBottomTAy.size = GBcc;
 grainBottomTAz.size = GBcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Obstacle::poissonRatio))/Obstacle::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Obstacle::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Obstacle::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainBottomEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, size_t start, size_t size) {
   evaluateGrainObstacle(start, size,
                     grainBottomContact.data, grainBottomContact.size,
                     grainBottomA.data,
                     grain.Px.data, grain.Py.data, grain.Pz.data,
                     floatX(bottomZ), floatX(Grain::radius),
                     grainBottomLocalAx.begin(), grainBottomLocalAy.begin(), grainBottomLocalAz.begin(),
                     grainBottomLocalBx.begin(), grainBottomLocalBy.begin(), grainBottomLocalBz.begin(),
                     floatX(K), floatX(Kb),
                     floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                     floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                     grain.Vx.data, grain.Vy.data, grain.Vz.data,
                     grain.AVx.data, grain.AVy.data, grain.AVz.data,
                     grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                     grainBottomFx.begin(), grainBottomFy.begin(), grainBottomFz.begin(),
                     grainBottomTAx.begin(), grainBottomTAy.begin(), grainBottomTAz.begin() );
 }, 1);

 grainBottomSumTime.start();
 float bottomForceZ = 0;
 for(size_t i=0; i<grainBottomContact.size; i++) { // Scalar scatter add
  size_t index = grainBottomContact[i];
  size_t a = grainBottomA[index];
  grain.Fx[a] += grainBottomFx[i];
  grain.Fy[a] += grainBottomFy[i];
  grain.Fz[a] += grainBottomFz[i];
  grain.Tx[a] += grainBottomTAx[i];
  grain.Ty[a] += grainBottomTAy[i];
  grain.Tz[a] += grainBottomTAz[i];
  bottomForceZ += grainBottomFz[i];
 }
 this->bottomForceZ = bottomForceZ;
 grainBottomSumTime.stop();
}
