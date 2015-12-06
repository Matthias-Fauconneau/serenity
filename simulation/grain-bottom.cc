// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

template<> String str(const v8ui& v) {
 return str(extract(v, 0), extract(v, 1), extract(v, 2), extract(v, 3),
                 extract(v, 4), extract(v, 5), extract(v, 6), extract(v, 7)); }

//FIXME: factorize Bottom and Top
void evaluateGrainObstacle(const size_t start, const size_t size,
                           const uint* grainObstacleContact, const size_t unused grainObstacleContactSize,
                           const ref<uint> grainObstacleA,
                           const float* grainPx, const float* grainPy, const float* grainPz,
                           const v8sf obstacleZ, const v8sf Gr,
                           float* const grainObstacleLocalAx, float* const grainObstacleLocalAy, float* const grainObstacleLocalAz,
                           float* const grainObstacleLocalBx, float* const grainObstacleLocalBy, float* const grainObstacleLocalBz,
                           const v8sf K, const v8sf Kb,
                           const v8sf staticFrictionStiffness, const v8sf dynamicFrictionCoefficient,
                           const v8sf staticFrictionLength, const v8sf staticFrictionSpeed,
                           const v8sf staticFrictionDamping,
                           const float* AVx, const float* AVy, const float* AVz,
                           const float* pAAVx, const float* pAAVy, const float* pAAVz,
                           const float* AQX, const float* AQY, const float* AQZ, const float* AQW,
                           float* const pFx, float* const pFy, float* const pFz,
                           float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const v8ui contacts = *(v8ui*)(grainObstacleContact+i);
  log(i, contacts);
  const v8ui A = gather(grainObstacleA, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const v8sf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const v8sf depth = (obstacleZ+Gr) - Az; // Top: Az - (obstacleZ-Gr);
  const v8sf Nx = _0f, Ny = _0f, Nz = _1f;
  const v8sf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const v8sf RBx = Ax, RBy = Ay, RBz = Az;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Obstacle

  // Tension
  const v8sf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const v8sf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const v8sf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy);
  const v8sf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz);
  const v8sf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx);
  // Damping
  const v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  const v8sf Fb = - Kb * sqrt(sqrt(depth)) * normalSpeed ; // Damping
  // Normal force
  const v8sf Fn = Fk + Fb;
  const v8sf NFx = Fn * Nx;
  const v8sf NFy = Fn * Ny;
  const v8sf NFz = Fn * Nz;

  // Dynamic friction
  // Tangent relative velocity
  const v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  const v8sf TRVx = RVx - RVn * Nx;
  const v8sf TRVy = RVy - RVn * Ny;
  const v8sf TRVz = RVz - RVn * Nz;
  const v8sf tangentRelativeSpeed = sqrt(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  const v8sf Fd = mask(greaterThan(tangentRelativeSpeed, _0f),
                       - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed);
  const v8sf FDx = Fd * TRVx;
  const v8sf FDy = Fd * TRVy;
  const v8sf FDz = Fd * TRVz;

  // Gather static frictions
  const v8sf oldLocalAx = gather(grainObstacleLocalAx, contacts);
  const v8sf oldLocalAy = gather(grainObstacleLocalAy, contacts);
  const v8sf oldLocalAz = gather(grainObstacleLocalAz, contacts);
  const v8sf oldLocalBx = gather(grainObstacleLocalBx, contacts);
  const v8sf oldLocalBy = gather(grainObstacleLocalBy, contacts);
  const v8sf oldLocalBz = gather(grainObstacleLocalBz, contacts);

  const v8sf QAx = gather(AQX, A);
  const v8sf QAy = gather(AQY, A);
  const v8sf QAz = gather(AQZ, A);
  const v8sf QAw = gather(AQW, A);
  const v8sf X1 = QAw*RAx + RAy*QAz - QAy*RAz;
  const v8sf Y1 = QAw*RAy + RAz*QAx - QAz*RAx;
  const v8sf Z1 = QAw*RAz + RAx*QAy - QAx*RAy;
  const v8sf W1 = - (RAx * QAx + RAy * QAy + RAz * QAz);
  const v8sf newLocalAx = QAw*X1 - (W1*QAx + QAy*Z1 - Y1*QAz);
  const v8sf newLocalAy = QAw*Y1 - (W1*QAy + QAz*X1 - Z1*QAx);
  const v8sf newLocalAz = QAw*Z1 - (W1*QAz + QAx*Y1 - X1*QAy);

  const v8sf newLocalBx = RBx;
  const v8sf newLocalBy = RBy;
  const v8sf newLocalBz = RBz;

  const v8ui keep = notEqual(oldLocalAx, _0f), reset = ~keep;
  v8sf localAx = merge(mask(keep, oldLocalAx), mask(reset, newLocalAx));
  const v8sf localAy = merge(mask(keep, oldLocalAy), mask(reset, newLocalAy));
  const v8sf localAz = merge(mask(keep, oldLocalAz), mask(reset, newLocalAz));
  const v8sf localBx = merge(mask(keep, oldLocalBx), mask(reset, newLocalBx));
  const v8sf localBy = merge(mask(keep, oldLocalBy), mask(reset, newLocalBy));
  const v8sf localBz = merge(mask(keep, oldLocalBz), mask(reset, newLocalBz));

  const v8sf X = QAw*localAx - (localAy*QAz - QAy*localAz);
  const v8sf Y = QAw*localAy - (localAz*QAx - QAz*localAx);
  const v8sf Z = QAw*localAz - (localAx*QAy - QAx*localAy);
  const v8sf W = localAx * QAx + localAy * QAy + localAz * QAz;
  const v8sf FRAx = QAw*X + W*QAx + QAy*Z - Y*QAz;
  const v8sf FRAy = QAw*Y + W*QAy + QAz*X - Z*QAx;
  const v8sf FRAz = QAw*Z + W*QAz + QAx*Y - X*QAy;
  const v8sf FRBx = localBx;
  const v8sf FRBy = localBy;
  const v8sf FRBz = localBz;

  const v8sf gAx = Ax + FRAx;
  const v8sf gAy = Ay + FRAy;
  const v8sf gAz = Az + FRAz;
  const v8sf gBx = FRBx;
  const v8sf gBy = FRBy;
  const v8sf gBz = FRBz;
  const v8sf Dx = gBx - gAx;
  const v8sf Dy = gBy - gAy;
  const v8sf Dz = gBz - gAz;
  const v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // Tangent offset
  const v8sf TOx = Dx - Dn * Nx;
  const v8sf TOy = Dy - Dn * Ny;
  const v8sf TOz = Dz - Dn * Nz;
  const v8sf tangentLength = sqrt(TOx*TOx+TOy*TOy+TOz*TOz);
  const v8sf Ks = staticFrictionStiffness * Fn;
  const v8sf Fs = Ks * tangentLength; // 0.1~1 fN
  // Spring direction
  const v8sf SDx = TOx / tangentLength;
  const v8sf SDy = TOy / tangentLength;
  const v8sf SDz = TOz / tangentLength;
  const v8ui hasTangentLength = greaterThan(tangentLength, _0f);
  const v8sf sfFb = mask(hasTangentLength,
                         staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz));
  const v8ui hasStaticFriction = greaterThan(staticFrictionLength, tangentLength)
                                              & greaterThan(staticFrictionSpeed, tangentRelativeSpeed);
  const v8sf sfFt = mask(hasStaticFriction, Fs - sfFb);
  const v8sf FSx = mask(hasTangentLength, sfFt * SDx);
  const v8sf FSy = mask(hasTangentLength, sfFt * SDy);
  const v8sf FSz = mask(hasTangentLength, sfFt * SDz);
  const v8sf FTx = FDx + FSx;
  const v8sf FTy = FDy + FSy;
  const v8sf FTz = FDz + FSz;
  // Resets contacts without static friction
  localAx = mask(hasStaticFriction, localAx); // FIXME use 1s (NaN) not 0s to flag resets

  *(v8sf*)(pFx+i) = NFx + FTx;
  *(v8sf*)(pFy+i) = NFy + FTy;
  *(v8sf*)(pFz+i) = NFz + FTz;
  *(v8sf*)(pTAx+i) = RAy*FTz - RAz*FTy;
  *(v8sf*)(pTAy+i) = RAz*FTx - RAx*FTz;
  *(v8sf*)(pTAz+i) = RAx*FTy - RAy*FTx;
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
 {
  swap(oldGrainBottomA, grainBottomA);
  swap(oldGrainBottomLocalAx, grainBottomLocalAx);
  swap(oldGrainBottomLocalAy, grainBottomLocalAy);
  swap(oldGrainBottomLocalAz, grainBottomLocalAz);
  swap(oldGrainBottomLocalBx, grainBottomLocalBx);
  swap(oldGrainBottomLocalBy, grainBottomLocalBy);
  swap(oldGrainBottomLocalBz, grainBottomLocalBz);

  static constexpr size_t averageGrainBottomContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainBottomContactCount);
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
    if(grain.Pz[i] > bottomZ+Grain::radius) continue;
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

 // TODO: verlet
 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainBottomA.size) > grainBottomContact.capacity) {
  grainBottomContact = buffer<uint>(align(simd, grainBottomA.size));
 }
 grainBottomContact.size = 0;
 auto filter = [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   v8ui A = *(v8ui*)(grainBottomA.data+i);
   v8sf Az = gather(grain.Pz, A);
   for(size_t k: range(simd)) {
    size_t j = i+k;
    if(j == grainBottomA.size) break /*2*/;
    if(extract(Az, k) < bottomZ+Grain::radius) {
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

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainBottomA.size); // Grain-Bottom contact count
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
                     grainBottomA,
                     grain.Px.data, grain.Py.data, grain.Pz.data,
                     float8(bottomZ), float8(Grain::radius),
                     grainBottomLocalAx.begin(), grainBottomLocalAy.begin(), grainBottomLocalAz.begin(),
                     grainBottomLocalBx.begin(), grainBottomLocalBy.begin(), grainBottomLocalBz.begin(),
                     float8(K), float8(Kb),
                     float8(staticFrictionStiffness), float8(dynamicFrictionCoefficient),
                     float8(staticFrictionLength), float8(staticFrictionSpeed), float8(staticFrictionDamping),
                     grain.Vx.data, grain.Vy.data, grain.Vz.data,
                     grain.AVx.data, grain.AVy.data, grain.AVz.data,
                     grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                     grainBottomFx.begin(), grainBottomFy.begin(), grainBottomFz.begin(),
                     grainBottomTAx.begin(), grainBottomTAy.begin(), grainBottomTAz.begin() );
 }, 1);

 grainBottomSumTime.start();
 float bottomForceZ = 0;
 for(size_t i=0; i<grainBottomA.size; i++) { // Scalar scatter add
  size_t a = grainBottomA[i];
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
