#pragma once
#include "simd.h"

//TODO: factorize Grain - Plate|Membrane
template<bool top> static inline void evaluateGrainObstacle(const size_t start, const size_t size,
                           const int* grainObstacleContact,
                           const int* grainObstacleA,
                           const float* grainPx, const float* grainPy, const float* grainPz,
                           const vXsf obstacleZ_Ar, const vXsf Ar,
                           float* const grainObstacleLocalAx, float* const grainObstacleLocalAy, float* const grainObstacleLocalAz,
                           float* const grainObstacleLocalBx, float* const grainObstacleLocalBy, float* const grainObstacleLocalBz,
                           const vXsf K, const vXsf Kb,
                           const vXsf staticFrictionStiffness, const vXsf frictionCoefficient,
                           const vXsf staticFrictionLength, const vXsf staticFrictionSpeed, const vXsf staticFrictionDamping,
                           const float* AVx, const float* AVy, const float* AVz,
                           const float* pAAVx, const float* pAAVy, const float* pAAVz,
                           const float* pAQX, const float* pAQY, const float* pAQZ, const float* pAQW,
                           float* const pFx, float* const pFy, float* const pFz,
                           float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = load(grainObstacleContact, i);
  const vXsi A = gather(grainObstacleA, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf depth = top ? Az - obstacleZ_Ar : obstacleZ_Ar - Az;
  const vXsf Nx = _0f, Ny = _0f, Nz = top ? -_1f : _1f;
  const vXsf ARx = - Ar  * Nx, ARy = - Ar * Ny, ARz = - Ar * Nz;
  const vXsf BRx = Ax + ARx, BRy = Ay + ARy, BRz = Az + ARz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Obstacle

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf RVx = gather(AVx, A) + (AAVy*ARz - AAVz*ARy);
  const vXsf RVy = gather(AVy, A) + (AAVz*ARx - AAVx*ARz);
  const vXsf RVz = gather(AVz, A) + (AAVx*ARy - AAVy*ARx);
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
  const vXsf Fd = - /*dynamicF*/frictionCoefficient * Fn / tangentRelativeSpeed;
  const vXsf FDx = mask3_fmadd(Fd, TRVx, _0f, div0);
  const vXsf FDy = mask3_fmadd(Fd, TRVy, _0f, div0);
  const vXsf FDz = mask3_fmadd(Fd, TRVz, _0f, div0);

  // Gather static frictions
  const vXsf oldLocalAx = gather(grainObstacleLocalAx, contacts);
  const vXsf oldLocalAy = gather(grainObstacleLocalAy, contacts);
  const vXsf oldLocalAz = gather(grainObstacleLocalAz, contacts);
  const vXsf oldLocalBx = gather(grainObstacleLocalBx, contacts);
  const vXsf oldLocalBy = gather(grainObstacleLocalBy, contacts);
  const vXsf oldLocalBz = gather(grainObstacleLocalBz, contacts);

  const vXsf AQx = gather(pAQX, A);
  const vXsf AQy = gather(pAQY, A);
  const vXsf AQz = gather(pAQZ, A);
  const vXsf AQw = gather(pAQW, A);
  vXsf newLocalAx, newLocalAy, newLocalAz;
  qapply(-AQx, -AQy, -AQz, AQw, ARx, ARy, ARz, newLocalAx, newLocalAy, newLocalAz);

  const vXsf newLocalBx = BRx, newLocalBy = BRy, newLocalBz = BRz;

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
  const vXsf localBx = blend(reset, oldLocalBx, newLocalBx);
  const vXsf localBy = blend(reset, oldLocalBy, newLocalBy);
  const vXsf localBz = blend(reset, oldLocalBz, newLocalBz);

  vXsf FRAx, FRAy, FRAz; qapply(AQx, AQy, AQz, AQw, localAx, localAy, localAz, FRAx, FRAy, FRAz);
  const vXsf FRBx = localBx, FRBy = localBy, FRBz = localBz;

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
  const vXsf Fs = Ks * tangentLength;
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
  store(pTAx, i, ARy*FTz - ARz*FTy);
  store(pTAy, i, ARz*FTx - ARx*FTz);
  //store(pTAz, i, ARx*FTy - ARy*FTx);
  store(pTAz, i, ARx*FTy - ARy*FTx - floatX(1) * Ar * AAVz /*Spin friction*/);
  //store(pTAx, i, _0f); store(pTAy, i, _0f); store(pTAz, i, _0f); // DEBUG
  // Scatter static frictions
  scatter(grainObstacleLocalAx, contacts, localAx);
  scatter(grainObstacleLocalAy, contacts, localAy);
  scatter(grainObstacleLocalAz, contacts, localAz);
  scatter(grainObstacleLocalBx, contacts, localBx);
  scatter(grainObstacleLocalBy, contacts, localBy);
  scatter(grainObstacleLocalBz, contacts, localBz);
 }
}
