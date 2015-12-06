#include "simulation.h"
#include "parallel.h"

static inline void qapply(v8sf Qx, v8sf Qy, v8sf Qz, v8sf Qw, v8sf Vx, v8sf Vy, v8sf Vz,
                     v8sf& QVx, v8sf& QVy, v8sf& QVz) {
 const v8sf X = Qw*Vx - Vy*Qz + Qy*Vz;
 const v8sf Y = Qw*Vy - Vz*Qx + Qz*Vx;
 const v8sf Z = Qw*Vz - Vx*Qy + Qx*Vy;
 const v8sf W = Vx * Qx + Vy * Qy + Vz * Qz;
 QVx = Qw*X + W*Qx + Qy*Z - Y*Qz;
 QVy = Qw*Y + W*Qy + Qz*X - Z*Qx;
 QVz = Qw*Z + W*Qz + Qx*Y - X*Qy;
}

static inline void evaluateGrainGrain(const size_t start, const size_t size,
                                     const uint* grainGrainContact, const size_t unused grainGrainContactSize,
                                     const uint* grainGrainA, const uint* grainGrainB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const v8sf Gr_Wr, const v8sf Gr, const v8sf Wr,
                                     float* const grainGrainLocalAx, float* const grainGrainLocalAy, float* const grainGrainLocalAz,
                                     float* const grainGrainLocalBx, float* const grainGrainLocalBy, float* const grainGrainLocalBz,
                                     const v8sf K, const v8sf Kb,
                                     const v8sf staticFrictionStiffness, const v8sf dynamicFrictionCoefficient,
                                     const v8sf staticFrictionLength, const v8sf staticFrictionSpeed,
                                     const v8sf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                     const float* pAQX, const float* pAQY, const float* pAQZ, const float* pAQW,
                                     const float* pBQX, const float* pBQY, const float* pBQZ, const float* pBQW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz,
                                     float* const pTBx, float* const pTBy, float* const pTBz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const v8ui contacts = *(v8ui*)(grainGrainContact+i);
  const v8ui A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const v8sf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const v8sf Bx = gather(grainPx, B), By = gather(grainPy, B), Bz = gather(grainPz, B);
  const v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const v8sf depth = Gr_Wr - length;
  const v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const v8sf ARx = - Gr  * Nx, ARy = - Gr * Ny, ARz = - Gr * Nz;
  const v8sf BRx = + Wr  * Nx, BRy = + Wr * Ny, BRz = + Wr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Grain

  // Tension
  const v8sf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const v8sf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const v8sf RVx = gather(AVx, A) + (AAVy*ARz - AAVz*ARy) - gather(BVx, B);
  const v8sf RVy = gather(AVy, A) + (AAVz*ARx - AAVx*ARz) - gather(BVy, B);
  const v8sf RVz = gather(AVz, A) + (AAVx*ARy - AAVy*ARx) - gather(BVz, B);
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
  const v8sf oldLocalAx = gather(grainGrainLocalAx, contacts);
  const v8sf oldLocalAy = gather(grainGrainLocalAy, contacts);
  const v8sf oldLocalAz = gather(grainGrainLocalAz, contacts);
  const v8sf oldLocalBx = gather(grainGrainLocalBx, contacts);
  const v8sf oldLocalBy = gather(grainGrainLocalBy, contacts);
  const v8sf oldLocalBz = gather(grainGrainLocalBz, contacts);

  const v8sf AQx = gather(pAQX, A);
  const v8sf AQy = gather(pAQY, A);
  const v8sf AQz = gather(pAQZ, A);
  const v8sf AQw = gather(pAQW, A);
  v8sf newLocalAx, newLocalAy, newLocalAz;
  qapply(-AQx, -AQy, -AQz, AQw, ARx, ARy, ARz, newLocalAx, newLocalAy, newLocalAz);

  const v8sf BQx = gather(pBQX, B);
  const v8sf BQy = gather(pBQY, B);
  const v8sf BQz = gather(pBQZ, B);
  const v8sf BQw = gather(pBQW, B);
  v8sf newLocalBx, newLocalBy, newLocalBz;
  qapply(-BQx, -BQy, -BQz, BQw, BRx, BRy, BRz, newLocalBx, newLocalBy, newLocalBz);

  const v8ui keep = notEqual(oldLocalAx, _0f), reset = ~keep;
  v8sf localAx = merge(mask(keep, oldLocalAx), mask(reset, newLocalAx));
  const v8sf localAy = merge(mask(keep, oldLocalAy), mask(reset, newLocalAy));
  const v8sf localAz = merge(mask(keep, oldLocalAz), mask(reset, newLocalAz));
  const v8sf localBx = merge(mask(keep, oldLocalBx), mask(reset, newLocalBx));
  const v8sf localBy = merge(mask(keep, oldLocalBy), mask(reset, newLocalBy));
  const v8sf localBz = merge(mask(keep, oldLocalBz), mask(reset, newLocalBz));

  v8sf FRAx, FRAy, FRAz; qapply(AQx, AQy, AQz, AQw, localAx, localAy, localAz, FRAx, FRAy, FRAz);
  v8sf FRBx, FRBy, FRBz; qapply(BQx, BQy, BQz, BQw, localBx, localBy, localBz, FRBx, FRBy, FRBz);

  const v8sf gAx = Ax + FRAx;
  const v8sf gAy = Ay + FRAy;
  const v8sf gAz = Az + FRAz;
  const v8sf gBx = Bx + FRBx;
  const v8sf gBy = By + FRBy;
  const v8sf gBz = Bz + FRBz;
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
  *(v8sf*)(pTAx+i) = ARy*FTz - ARz*FTy;
  *(v8sf*)(pTAy+i) = ARz*FTx - ARx*FTz;
  *(v8sf*)(pTAz+i) = ARx*FTy - ARy*FTx;
  *(v8sf*)(pTBx+i) = BRz*FTy - BRy*FTz;
  *(v8sf*)(pTBy+i) = BRx*FTz - BRz*FTx;
  *(v8sf*)(pTBz+i) = BRy*FTx - BRx*FTy;
  // Scatter static frictions
  scatter(grainGrainLocalAx, contacts, localAx);
  scatter(grainGrainLocalAy, contacts, localAy);
  scatter(grainGrainLocalAz, contacts, localAz);
  scatter(grainGrainLocalBx, contacts, localBx);
  scatter(grainGrainLocalBy, contacts, localBy);
  scatter(grainGrainLocalBz, contacts, localBz);
 }
}

void Simulation::stepGrainGrain() {
 if(!grain.count) return;
 if(grainGrainGlobalMinD <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
  grainGrainSearchTime.start();

  vec3 min, max; domain(min, max);
  Lattice<uint16> lattice(sqrt(3.)/(2*Grain::radius), min, max);
  for(size_t i: range(grain.count)) {
   lattice.cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i;
  }

  const float verletDistance = 2*(2*Grain::radius/sqrt(3.)); // > Grain::radius + Grain::radius
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  float minD = __builtin_inff();

  const int Y = lattice.size.y, X = lattice.size.x;
  const uint16* latticeNeighbours[62];
  size_t i = 0;
  for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
   latticeNeighbours[i++] = lattice.base.data + z*Y*X + y*X + x;
  }
  assert(i==62);

  swap(oldGrainGrainA, grainGrainA);
  swap(oldGrainGrainB, grainGrainB);
  swap(oldGrainGrainLocalAx, grainGrainLocalAx);
  swap(oldGrainGrainLocalAy, grainGrainLocalAy);
  swap(oldGrainGrainLocalAz, grainGrainLocalAz);
  swap(oldGrainGrainLocalBx, grainGrainLocalBx);
  swap(oldGrainGrainLocalBy, grainGrainLocalBy);
  swap(oldGrainGrainLocalBz, grainGrainLocalBz);

  static constexpr size_t averageGrainGrainContactCount = 7;
  size_t GGcc = align(simd, grain.count * averageGrainGrainContactCount);
  if(GGcc > grainGrainA.capacity) {
   grainGrainA = buffer<uint>(GGcc, 0);
   grainGrainB = buffer<uint>(GGcc, 0);
   grainGrainLocalAx = buffer<float>(GGcc, 0);
   grainGrainLocalAy = buffer<float>(GGcc, 0);
   grainGrainLocalAz = buffer<float>(GGcc, 0);
   grainGrainLocalBx = buffer<float>(GGcc, 0);
   grainGrainLocalBy = buffer<float>(GGcc, 0);
   grainGrainLocalBz = buffer<float>(GGcc, 0);
  }
  grainGrainA.size = 0;
  grainGrainB.size = 0;
  grainGrainLocalAx.size = 0;
  grainGrainLocalAy.size = 0;
  grainGrainLocalAz.size = 0;
  grainGrainLocalBx.size = 0;
  grainGrainLocalBy.size = 0;
  grainGrainLocalBz.size = 0;

  size_t grainGrainI = 0; // Index of first contact with A in old grainGrain[Local]A|B list
  for(size_t a: range(grain.count)) {
   size_t offset = lattice.index(grain.Px[a], grain.Py[a], grain.Pz[a]);

   // Neighbours
   for(size_t n: range(62)) {
    size_t b = *(latticeNeighbours[n] + offset);
    if(!b) continue;
    b--;
    float d = sqrt(sq(grain.Px[a]-grain.Px[b])
                      + sq(grain.Py[a]-grain.Py[b])
                      + sq(grain.Pz[a]-grain.Pz[b])); //TODO: SIMD //FIXME: fails with Ofast?
    if(d > verletDistance) { minD=::min(minD, d); continue; }
    grainGrainA.append( a );
    grainGrainB.append( b );
    for(size_t k = 0;; k++) {
     size_t j = grainGrainI+k;
     if(j >= oldGrainGrainA.size || oldGrainGrainA[j] != a) break;
     if(oldGrainGrainB[j] == b) { // Repack existing friction
      grainGrainLocalAx.append( oldGrainGrainLocalAx[j] );
      grainGrainLocalAy.append( oldGrainGrainLocalAy[j] );
      grainGrainLocalAz.append( oldGrainGrainLocalAz[j] );
      grainGrainLocalBx.append( oldGrainGrainLocalBx[j] );
      grainGrainLocalBy.append( oldGrainGrainLocalBy[j] );
      grainGrainLocalBz.append( oldGrainGrainLocalBz[j] );
      goto break_;
     }
    } /*else*/ { // New contact
     // Appends zero to reserve slot. Zero flags contacts for evaluation.
     // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
     grainGrainLocalAx.append( 0 );
     grainGrainLocalAy.append( 0 );
     grainGrainLocalAz.append( 0 );
     grainGrainLocalBx.append( 0 );
     grainGrainLocalBy.append( 0 );
     grainGrainLocalBz.append( 0 );
    }
    break_:;
   }
   while(grainGrainI < oldGrainGrainA.size && oldGrainGrainA[grainGrainI] == a)
    grainGrainI++;
  }

  assert_(align(simd, grainGrainA.size+1) <= grainGrainA.capacity);
  for(size_t i=grainGrainA.size; i<align(simd, grainGrainA.size+1); i++) grainGrainA.begin()[i] = 0;
  for(size_t i=grainGrainB.size; i<align(simd, grainGrainB.size+1); i++) grainGrainB.begin()[i] = 0;

  grainGrainGlobalMinD = minD - 2*Grain::radius;
  if(grainGrainGlobalMinD < 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD);

  grainGrainSearchTime.stop();
  //log("grain-grain", grainGrainSkipped);
  grainGrainSkipped=0;
 } else grainGrainSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainGrainA.size) > grainGrainContact.capacity) {
  grainGrainContact = buffer<uint>(align(simd, grainGrainA.size));
 }
 grainGrainContact.size = 0;
 grainGrainFilterTime += parallel_chunk(align(simd, grainGrainA.size)/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    v8ui A = *(v8ui*)(grainGrainA.data+i), B = *(v8ui*)(grainGrainB.data+i);
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
    v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    v8sf depth = float8(Grain::radius+Grain::radius) - length;
    for(size_t k: range(8)) {
     size_t j = i+k;
     if(j == grainGrainA.size) break /*2*/;
     if(extract(depth, k) > 0) {
      // Creates a map from packed contact to index into unpacked contact list (indirect reference)
      // Instead of packing (copying) the unpacked list to a packed contact list
      // To keep track of where to write back (unpacked) contact positions (for static friction)
      // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
      grainGrainContact.append( j );
     } else {
      // Resets contact (static friction spring)
      grainGrainLocalAx[j] = 0; /*grainGrainLocalAy[j] = 0; grainGrainLocalAz[j] = 0;
      grainGrainLocalBx[j] = 0; grainGrainLocalBy[j] = 0; grainGrainLocalBz[j] = 0;*/
     }
    }
   }
 }, 1);
 for(size_t i=grainGrainContact.size; i<align(simd, grainGrainContact.size); i++)
  grainGrainContact.begin()[i] = 0;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GGcc = align(simd, grainGrainContact.size); // Grain-Grain contact count
 if(GGcc > grainGrainFx.capacity) {
  grainGrainFx = buffer<float>(GGcc);
  grainGrainFy = buffer<float>(GGcc);
  grainGrainFz = buffer<float>(GGcc);
  grainGrainTAx = buffer<float>(GGcc);
  grainGrainTAy = buffer<float>(GGcc);
  grainGrainTAz = buffer<float>(GGcc);
  grainGrainTBx = buffer<float>(GGcc);
  grainGrainTBy = buffer<float>(GGcc);
  grainGrainTBz = buffer<float>(GGcc);
 }
 grainGrainFx.size = GGcc;
 grainGrainFy.size = GGcc;
 grainGrainFz.size = GGcc;
 grainGrainTAx.size = GGcc;
 grainGrainTAy.size = GGcc;
 grainGrainTAz.size = GGcc;
 grainGrainTBx.size = GGcc;
 grainGrainTBy.size = GGcc;
 grainGrainTBz.size = GGcc;

 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Grain::poissonRatio))/Grain::elasticModulus);
 constexpr float R = 1/(Grain::curvature+Grain::curvature);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass+1/Grain::mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainGrainEvaluateTime += parallel_chunk(GGcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainGrain(start, size,
                      grainGrainContact.data, grainGrainContact.size,
                      grainGrainA.data, grainGrainB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      float8(Grain::radius+Grain::radius), float8(Grain::radius), float8(Grain::radius),
                      grainGrainLocalAx.begin(), grainGrainLocalAy.begin(), grainGrainLocalAz.begin(),
                      grainGrainLocalBx.begin(), grainGrainLocalBy.begin(), grainGrainLocalBz.begin(),
                      float8(K), float8(Kb),
                      float8(staticFrictionStiffness), float8(dynamicFrictionCoefficient),
                      float8(staticFrictionLength), float8(staticFrictionSpeed), float8(staticFrictionDamping),
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grainGrainFx.begin(), grainGrainFy.begin(), grainGrainFz.begin(),
                      grainGrainTAx.begin(), grainGrainTAy.begin(), grainGrainTAz.begin(),
                      grainGrainTBx.begin(), grainGrainTBy.begin(), grainGrainTBz.begin() );
 }, 1);

 grainGrainSumTime.start();
  for(size_t i = 0; i < grainGrainContact.size; i++) { // Scalar scatter add
   size_t index = grainGrainContact[i];
   size_t a = grainGrainA[index];
   size_t b = grainGrainB[index];
   grain.Fx[a] += grainGrainFx[i];
   grain.Fx[b] -= grainGrainFx[i];
   grain.Fy[a] += grainGrainFy[i];
   grain.Fy[b] -= grainGrainFy[i];
   grain.Fz[a] += grainGrainFz[i];
   grain.Fz[b] -= grainGrainFz[i];

   grain.Tx[a] += grainGrainTAx[i];
   grain.Ty[a] += grainGrainTAy[i];
   grain.Tz[a] += grainGrainTAz[i];
   grain.Tx[b] += grainGrainTBx[i];
   grain.Ty[b] += grainGrainTBy[i];
   grain.Tz[b] += grainGrainTBz[i];
  }
  grainGrainSumTime.stop();
}
