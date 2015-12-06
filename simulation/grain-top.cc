// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

//FIXME: factorize Top and Bottom
void evaluateGrainObstacle(const size_t start, const size_t size,
                                     const uint* grainObstacleContact, const size_t unused grainObstacleContactSize,
                                     const uint* grainObstacleA,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const v8sf obstacleZ, const v8sf Gr,
                                     float* const grainObstacleLocalAx, float* const grainObstacleLocalAy, float* const grainObstacleLocalAz,
                                     float* const grainObstacleLocalBx, float* const grainObstacleLocalBy, float* const grainObstacleLocalBz,
                                     const v8sf K, const v8sf Kb,
                                     const v8sf staticFrictionStiffness, const v8sf dynamicFrictionCoefficient,
                                     const v8sf staticFrictionLength, const v8sf staticFrictionSpeed,
                                     const v8sf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                     const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                     const float* ArotationW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const v8ui contacts = *(v8ui*)(grainObstacleContact+i);
  const v8ui A = gather(grainObstacleA, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const v8sf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const v8sf depth = Az - (obstacleZ-Gr); // Bottom: (obstacleZ+Gr) - Az
  const v8sf Nx = _0f, Ny = _0f, Nz = -_1f;
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

  const v8sf QAx = gather(ArotationX, A);
  const v8sf QAy = gather(ArotationY, A);
  const v8sf QAz = gather(ArotationZ, A);
  const v8sf QAw = gather(ArotationW, A);
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

void Simulation::stepGrainTop() {
 {
  swap(oldGrainTopA, grainTopA);
  swap(oldGrainTopLocalAx, grainTopLocalAx);
  swap(oldGrainTopLocalAy, grainTopLocalAy);
  swap(oldGrainTopLocalAz, grainTopLocalAz);
  swap(oldGrainTopLocalBx, grainTopLocalBx);
  swap(oldGrainTopLocalBy, grainTopLocalBy);
  swap(oldGrainTopLocalBz, grainTopLocalBz);

  static constexpr size_t averageGrainTopContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainTopContactCount);
  if(GBcc > grainTopA.capacity) {
   grainTopA = buffer<uint>(GBcc, 0);
   grainTopLocalAx = buffer<float>(GBcc, 0);
   grainTopLocalAy = buffer<float>(GBcc, 0);
   grainTopLocalAz = buffer<float>(GBcc, 0);
   grainTopLocalBx = buffer<float>(GBcc, 0);
   grainTopLocalBy = buffer<float>(GBcc, 0);
   grainTopLocalBz = buffer<float>(GBcc, 0);
  }
  grainTopA.size = 0;
  grainTopLocalAx.size = 0;
  grainTopLocalAy.size = 0;
  grainTopLocalAz.size = 0;
  grainTopLocalBx.size = 0;
  grainTopLocalBy.size = 0;
  grainTopLocalBz.size = 0;

  size_t grainTopI = 0; // Index of first contact with A in old grainTop[Local]A|B list
  auto search = [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
    if(grain.Pz[i] < topZ-Grain::radius) continue;
    grainTopA.append( i ); // Grain
    size_t j = grainTopI;
    if(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i) {
     // Repack existing friction
     grainTopLocalAx.append( oldGrainTopLocalAx[j] );
     grainTopLocalAy.append( oldGrainTopLocalAy[j] );
     grainTopLocalAz.append( oldGrainTopLocalAz[j] );
     grainTopLocalBx.append( oldGrainTopLocalBx[j] );
     grainTopLocalBy.append( oldGrainTopLocalBy[j] );
     grainTopLocalBz.append( oldGrainTopLocalBz[j] );
    } else { // New contact
     // Appends zero to reserve slot. Zero flags contacts for evaluation.
     // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
     grainTopLocalAx.append( 0 );
     grainTopLocalAy.append( 0 );
     grainTopLocalAz.append( 0 );
     grainTopLocalBx.append( 0 );
     grainTopLocalBy.append( 0 );
     grainTopLocalBz.append( 0 );
    }
    while(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i)
     grainTopI++;
   }
  };
  grainTopFilterTime += parallel_chunk(grain.count, search, 1);

  for(size_t i=grainTopA.size; i<align(simd, grainTopA.size); i++)
   grainTopA.begin()[i] = 0;
 }

 // TODO: verlet
 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainTopA.size) > grainTopContact.capacity) {
  grainTopContact = buffer<uint>(align(simd, grainTopA.size));
 }
 grainTopContact.size = 0;
 auto filter =  [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   v8ui A = *(v8ui*)(grainTopA.data+i);
   v8sf Az = gather(grain.Pz, A);
   for(size_t k: range(simd)) {
    size_t j = i+k;
    if(j == grainTopA.size) break /*2*/;
    if(extract(Az, k) < bottomZ+Grain::radius) {
     // Creates a map from packed contact to index into unpacked contact list (indirect reference)
     // Instead of packing (copying) the unpacked list to a packed contact list
     // To keep track of where to write back (unpacked) contact positions (for static friction)
     // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
     grainTopContact.append( j );
    } else {
     // Resets contact (static friction spring)
     grainTopLocalAx[j] = 0;
    }
   }
  }
 };
 grainTopFilterTime += parallel_chunk(align(simd, grainTopA.size)/simd, filter, 1);
 for(size_t i=grainTopContact.size; i<align(simd, grainTopContact.size); i++)
  grainTopContact.begin()[i] = grainTopA.size;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainTopA.size); // Grain-Wire contact count
 if(GBcc > grainTopFx.capacity) {
  grainTopFx = buffer<float>(GBcc);
  grainTopFy = buffer<float>(GBcc);
  grainTopFz = buffer<float>(GBcc);
  grainTopTAx = buffer<float>(GBcc);
  grainTopTAy = buffer<float>(GBcc);
  grainTopTAz = buffer<float>(GBcc);
 }
 grainTopFx.size = GBcc;
 grainTopFy.size = GBcc;
 grainTopFz.size = GBcc;
 grainTopTAx.size = GBcc;
 grainTopTAy.size = GBcc;
 grainTopTAz.size = GBcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Obstacle::poissonRatio))/Obstacle::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Obstacle::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Obstacle::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainTopEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, size_t start, size_t size) {
   evaluateGrainObstacle(start, size,
                     grainTopContact.data, grainTopContact.size,
                     grainTopA.data,
                     grain.Px.data, grain.Py.data, grain.Pz.data,
                     float8(bottomZ), float8(Grain::radius),
                     grainTopLocalAx.begin(), grainTopLocalAy.begin(), grainTopLocalAz.begin(),
                     grainTopLocalBx.begin(), grainTopLocalBy.begin(), grainTopLocalBz.begin(),
                     float8(K), float8(Kb),
                     float8(staticFrictionStiffness), float8(dynamicFrictionCoefficient),
                     float8(staticFrictionLength), float8(staticFrictionSpeed), float8(staticFrictionDamping),
                     grain.Vx.data, grain.Vy.data, grain.Vz.data,
                     wire.Vx.data, wire.Vy.data, wire.Vz.data,
                     grain.AVx.data, grain.AVy.data, grain.AVz.data,
                     grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                     grainTopFx.begin(), grainTopFy.begin(), grainTopFz.begin(),
                     grainTopTAx.begin(), grainTopTAy.begin(), grainTopTAz.begin() );
 }, 1);

 grainTopSumTime.start();
 float topForceZ = 0;
 for(size_t i=0; i<grainTopA.size; i++) { // Scalar scatter add
  size_t a = grainTopA[i];
  grain.Fx[a] += grainTopFx[i];
  grain.Fy[a] += grainTopFy[i];
  grain.Fz[a] += grainTopFz[i];
  grain.Tx[a] += grainTopTAx[i];
  grain.Ty[a] += grainTopTAy[i];
  grain.Tz[a] += grainTopTAz[i];
  topForceZ -= grainTopFz[i];
 }
 this->topForceZ = topForceZ;
 grainTopSumTime.stop();
}
