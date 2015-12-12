#include "simulation.h"
#include "lattice.h"
#include "parallel.h"

static inline void qapply(vXsf Qx, vXsf Qy, vXsf Qz, vXsf Qw, vXsf Vx, vXsf Vy, vXsf Vz,
                     vXsf& QVx, vXsf& QVy, vXsf& QVz) {
 const vXsf X = Qw*Vx - Vy*Qz + Qy*Vz;
 const vXsf Y = Qw*Vy - Vz*Qx + Qz*Vx;
 const vXsf Z = Qw*Vz - Vx*Qy + Qx*Vy;
 const vXsf W = Vx * Qx + Vy * Qy + Vz * Qz;
 QVx = Qw*X + W*Qx + Qy*Z - Y*Qz;
 QVy = Qw*Y + W*Qy + Qz*X - Z*Qx;
 QVz = Qw*Z + W*Qz + Qx*Y - X*Qy;
}

static inline void evaluateGrainGrain(const int start, const int size,
                                     const uint* grainGrainContact,
                                     const uint* grainGrainA, const uint* grainGrainB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const vXsf Gr_Wr, const vXsf Gr, const vXsf Wr,
                                     float* const grainGrainLocalAx, float* const grainGrainLocalAy, float* const grainGrainLocalAz,
                                     float* const grainGrainLocalBx, float* const grainGrainLocalBy, float* const grainGrainLocalBz,
                                     const vXsf K, const vXsf Kb,
                                     const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                     const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                     const vXsf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                      const float* pBAVx, const float* pBAVy, const float* pBAVz,
                                     const float* pAQX, const float* pAQY, const float* pAQZ, const float* pAQW,
                                     const float* pBQX, const float* pBQY, const float* pBQZ, const float* pBQW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz,
                                     float* const pTBx, float* const pTBy, float* const pTBz) {
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXui contacts = *(vXui*)(grainGrainContact+i);
  const vXui A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf Bx = gather(grainPx, B), By = gather(grainPy, B), Bz = gather(grainPz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Gr_Wr - length;
  const vXsf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const vXsf ARx = - Gr  * Nx, ARy = - Gr * Ny, ARz = - Gr * Nz;
  const vXsf BRx = + Wr  * Nx, BRy = + Wr * Ny, BRz = + Wr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Grain

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf BAVx = gather(pBAVx, B), BAVy = gather(pBAVy, B), BAVz = gather(pBAVz, B);
  const vXsf RVx = gather(AVx, A) + (AAVy*ARz - AAVz*ARy) - gather(BVx, B) - (BAVy*BRz - BAVz*BRy);
  const vXsf RVy = gather(AVy, A) + (AAVz*ARx - AAVx*ARz) - gather(BVy, B) - (BAVz*BRx - BAVx*BRz);
  const vXsf RVz = gather(AVz, A) + (AAVx*ARy - AAVy*ARx) - gather(BVz, B) - (BAVx*BRy - BAVy*BRx);
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
  const vXsf oldLocalAx = gather(grainGrainLocalAx, contacts);
  const vXsf oldLocalAy = gather(grainGrainLocalAy, contacts);
  const vXsf oldLocalAz = gather(grainGrainLocalAz, contacts);
  const vXsf oldLocalBx = gather(grainGrainLocalBx, contacts);
  const vXsf oldLocalBy = gather(grainGrainLocalBy, contacts);
  const vXsf oldLocalBz = gather(grainGrainLocalBz, contacts);

  const vXsf AQx = gather(pAQX, A);
  const vXsf AQy = gather(pAQY, A);
  const vXsf AQz = gather(pAQZ, A);
  const vXsf AQw = gather(pAQW, A);
  vXsf newLocalAx, newLocalAy, newLocalAz;
  qapply(-AQx, -AQy, -AQz, AQw, ARx, ARy, ARz, newLocalAx, newLocalAy, newLocalAz);

  const vXsf BQx = gather(pBQX, B);
  const vXsf BQy = gather(pBQY, B);
  const vXsf BQz = gather(pBQZ, B);
  const vXsf BQw = gather(pBQW, B);
  vXsf newLocalBx, newLocalBy, newLocalBz;
  qapply(-BQx, -BQy, -BQz, BQw, BRx, BRy, BRz, newLocalBx, newLocalBy, newLocalBz);

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
  const vXsf localBx = blend(reset, oldLocalBx, newLocalBx);
  const vXsf localBy = blend(reset, oldLocalBy, newLocalBy);
  const vXsf localBz = blend(reset, oldLocalBz, newLocalBz);

  vXsf FRAx, FRAy, FRAz; qapply(AQx, AQy, AQz, AQw, localAx, localAy, localAz, FRAx, FRAy, FRAz);
  vXsf FRBx, FRBy, FRBz; qapply(BQx, BQy, BQz, BQw, localBx, localBy, localBz, FRBx, FRBy, FRBz);

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
  *(vXsf*)(pTAx+i) = ARy*FTz - ARz*FTy;
  *(vXsf*)(pTAy+i) = ARz*FTx - ARx*FTz;
  *(vXsf*)(pTAz+i) = ARx*FTy - ARy*FTx;
  *(vXsf*)(pTBx+i) = BRz*FTy - BRy*FTz;
  *(vXsf*)(pTBy+i) = BRx*FTz - BRz*FTx;
  *(vXsf*)(pTBz+i) = BRy*FTx - BRx*FTy;
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
 grainGrainTotalTime.start();
 if(grainGrainGlobalMinD <= 0) { // Re-evaluates verlet lists (using a lattice for grains)

  //vec3 min, max; domainGrain(min, max);
  //vec3 min = vec3(vec2(-membrane.radius), 0), max = vec3(vec2(membrane.radius), membrane.height);
#if 1
  const float R = membrane.radius*(1+1./2);
  vec3 min = vec3(vec2(-R), 0), max = vec3(vec2(R), membrane.height);
#if 0
  {const float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
   for(size_t i=0; i<grain.count; i++) {
    assert_(min.x <= Px[i], min.x, Px[i], log2(Px[i]/min.x-1));
    assert_(min.y <= Py[i], min.y, Py[i], log2(Py[i]/min.y-1));
    assert_(min.z <= Pz[i], min.z, Pz[i], log2(Pz[i]/min.z-1));
    assert_(Px[i] <= max.x, Px[i], max.x, log2(Px[i]/max.x-1));
    assert_(Py[i] <= max.y, Py[i], max.y, log2(Py[i]/max.y-1));
    assert_(Pz[i] <= max.z, Pz[i], max.z, log2(Pz[i]/min.z-1));
   }
  }
#endif
#endif

  memoryTime.start();
  Lattice<uint16> lattice(sqrt(3.)/(2*Grain::radius), min, max);
  memoryTime.stop();
  grainGrainLatticeTime.start();
  for(size_t i: range(grain.count)) {
   lattice.cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i;
  }

  const float verletDistance = 2*2*Grain::radius/sqrt(3.); // > Grain::radius + Grain::radius
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  //float minD = verletDistance; //__builtin_inff(); // With lattice, a nearest > verletDistance might be skipped

  const int Y = lattice.size.y, X = lattice.size.x;
  const uint16* latticeNeighbours[62];
  size_t i = 0;
  for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
   latticeNeighbours[i++] = lattice.base.data + z*Y*X + y*X + x;
  }
  assert(i==62);
  grainGrainLatticeTime.stop();
  grainGrainSearchTime.start();

  swap(oldGrainGrainA, grainGrainA);
  swap(oldGrainGrainB, grainGrainB);
  swap(oldGrainGrainLocalAx, grainGrainLocalAx);
  swap(oldGrainGrainLocalAy, grainGrainLocalAy);
  swap(oldGrainGrainLocalAz, grainGrainLocalAz);
  swap(oldGrainGrainLocalBx, grainGrainLocalBx);
  swap(oldGrainGrainLocalBy, grainGrainLocalBy);
  swap(oldGrainGrainLocalBz, grainGrainLocalBz);

  static constexpr size_t averageGrainGrainContactCount = 7;
  size_t GGcc = align(simd, grain.count * averageGrainGrainContactCount +1);
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
    if(d > verletDistance) { /*minD=::min(minD, d);*/ continue; }
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

  grainGrainGlobalMinD = verletDistance/*minD*/ - 2*Grain::radius;
  if(grainGrainGlobalMinD <= 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD);

  //log("grain-grain", grainGrainSkipped);
  grainGrainSkipped=0;
  grainGrainSearchTime.stop();
 } else grainGrainSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainGrainA.size) > grainGrainContact.capacity) {
  grainGrainContact = buffer<uint>(align(simd, grainGrainA.size));
 }
 grainGrainContact.size = 0;
 grainGrainFilterTime += parallel_chunk(align(simd, grainGrainA.size)/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    vXui A = *(vXui*)(grainGrainA.data+i), B = *(vXui*)(grainGrainB.data+i);
    vXsf Ax = gather(grain.Px.data, A), Ay = gather(grain.Py.data, A), Az = gather(grain.Pz.data, A);
    vXsf Bx = gather(grain.Px.data, B), By = gather(grain.Py.data, B), Bz = gather(grain.Pz.data, B);
    vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    vXsf depth = floatX(Grain::radius+Grain::radius) - length;
    for(size_t k: range(simd)) {
     size_t j = i+k;
     if(j == grainGrainA.size) break /*2*/;
     if(extract(depth, k) >= 0) {
      // Creates a map from packed contact to index into unpacked contact list (indirect reference)
      // Instead of packing (copying) the unpacked list to a packed contact list
      // To keep track of where to write back (unpacked) contact positions (for static friction)
      // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
      grainGrainContact.append( j );
     } else {
      // Resets contact (static friction spring)
      grainGrainLocalAx[j] = 0;
     }
    }
   }
 }, 1);
 for(size_t i=grainGrainContact.size; i<align(simd, grainGrainContact.size); i++)
  grainGrainContact.begin()[i] = grainGrainA.size;;

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
 grainGrainEvaluateTime += parallel_chunk(GGcc/simd, [this, K, Kb](uint, int start, int size) {
    evaluateGrainGrain(start, size,
                      grainGrainContact.data,
                      grainGrainA.data, grainGrainB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      floatX(Grain::radius+Grain::radius), floatX(Grain::radius), floatX(Grain::radius),
                      grainGrainLocalAx.begin(), grainGrainLocalAy.begin(), grainGrainLocalAz.begin(),
                      grainGrainLocalBx.begin(), grainGrainLocalBy.begin(), grainGrainLocalBz.begin(),
                      floatX(K), floatX(Kb),
                      floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                      floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grainGrainFx.begin(), grainGrainFy.begin(), grainGrainFz.begin(),
                      grainGrainTAx.begin(), grainGrainTAy.begin(), grainGrainTAz.begin(),
                      grainGrainTBx.begin(), grainGrainTBy.begin(), grainGrainTBz.begin() );
 });

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
#if 1
{ // Domain coherence profile
 const size_t threadCount = 60;
 const size_t jobCount = grainGrainContact.size;
 if(jobCount) {
  const size_t chunkSize = ::max(1ul, jobCount/threadCount);
  const size_t chunkCount = (jobCount+chunkSize-1)/chunkSize;
  uint domainsAStart[chunkCount], domainsAStop[chunkCount];
  uint domainsBStart[chunkCount], domainsBStop[chunkCount];
  // //
  for(size_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
   domainsAStart[chunkIndex] = grain.count;
   domainsAStop[chunkIndex] = 0;
   domainsBStart[chunkIndex] = grain.count;
   domainsBStop[chunkIndex] = 0;
   for(size_t i = chunkIndex*chunkSize; i < min(jobCount, (chunkIndex+1)*chunkSize); i++) {
    size_t index = grainGrainContact[i];
    uint a = grainGrainA[index];
    uint b = grainGrainB[index];
    domainsAStart[chunkIndex] = min(domainsAStart[chunkIndex], a);
    domainsAStop[chunkIndex]= max(domainsAStop[chunkIndex], a);
    domainsBStart[chunkIndex]= min(domainsBStart[chunkIndex], b);
    domainsBStop[chunkIndex]= max(domainsBStop[chunkIndex], b);
   }
  }
  // --
  size_t lastIndex = 0;
  for(size_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
   if(domainsAStart[chunkIndex] > lastIndex) lastIndex = domainsAStart[chunkIndex];
   if(domainsAStop[chunkIndex] > lastIndex) lastIndex = domainsAStop[chunkIndex];
   if(domainsBStart[chunkIndex] > lastIndex) lastIndex = domainsBStart[chunkIndex];
   if(domainsBStop[chunkIndex] > lastIndex) lastIndex = domainsBStop[chunkIndex];
  }
  size_t copyCount = 0, copyCountSum = 0, domainCount = 0, maxCopyCount = 0;
  size_t minDomainSize = -1, maxDomainSize = 0, sumDomainSize = 0;
  for(int index = -1; index<(int)lastIndex;) {
   size_t nextIndex = -1;
   for(size_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
    if((int)domainsAStart[chunkIndex] > index && domainsAStart[chunkIndex] < nextIndex)
     nextIndex = domainsAStart[chunkIndex];
    if((int)domainsAStop[chunkIndex] > index && domainsAStop[chunkIndex] < nextIndex)
     nextIndex = domainsAStop[chunkIndex];
    if((int)domainsBStart[chunkIndex] > index && domainsBStart[chunkIndex] < nextIndex)
     nextIndex = domainsBStart[chunkIndex];
    if((int)domainsBStop[chunkIndex] > index && domainsBStop[chunkIndex] < nextIndex)
     nextIndex = domainsBStop[chunkIndex];
   }
   for(size_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
    if(domainsAStart[chunkIndex] == nextIndex) {
     copyCount++;
     if(copyCount > maxCopyCount) maxCopyCount = copyCount;
    }
    if(domainsAStop[chunkIndex] == nextIndex) {
     copyCount--;
    }
    if(domainsBStart[chunkIndex] == nextIndex) {
     copyCount++;
     if(copyCount > maxCopyCount) maxCopyCount = copyCount;
    }
    if(domainsBStop[chunkIndex] == nextIndex) {
     copyCount--;
    }
   }
   copyCountSum += copyCount;
   domainCount++;
   size_t domainSize = nextIndex - index;
   minDomainSize = ::min(minDomainSize, domainSize);
   maxDomainSize = ::max(maxDomainSize, domainSize);
   sumDomainSize += domainSize;
   index = nextIndex;
  }
  log("max", maxCopyCount, "avg", domainCount?copyCountSum/domainCount:0, "#", domainCount,
      "minSize", minDomainSize, "maxSize", maxDomainSize, "total", sumDomainSize,
      "avgSize", domainCount?sumDomainSize/domainCount:0, "=Size", sumDomainSize/chunkCount);
  assert_(copyCount == 0, copyCount);
  //const size_t elementCount = grain.count;
  /*const size_t domainSize = ::max(1, elementCount/N);
 const size_t domainCount = (elementCount+chunkSize-1)/chunkSize;*/
/*  const size_t domainCount = chunkCount;
  const size_t domainSize = (elementCount+domainCount-1)/domainCount;
  size_t inDomainA = 0, outOfDomainA = 0, inDomainB = 0, outOfDomainB = 0, inDomainBoth = 0, outOfDomainAny = 0, outOfDomainBoth = 0;
  for(size_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
   const size_t domainIndex = chunkIndex;
   const size_t domainStart = domainIndex * domainSize;
   const size_t domainEnd = (domainIndex+1) * domainSize;
   //domainEnd = ::min(domainEnd, elementCount);
   for(size_t i = chunkIndex*chunkSize; i < min(jobCount, (chunkIndex+1)*chunkSize); i++) {
    size_t index = grainGrainContact[i];
    size_t a = grainGrainA[index];
    size_t b = grainGrainB[index];
    if(a >= domainStart && a<domainEnd) inDomainA++; else outOfDomainA++;
    if(b >= domainStart && b<domainEnd) inDomainB++; else  outOfDomainB++;
    if((a >= domainStart && a<domainEnd) && (b >= domainStart && b<domainEnd)) inDomainBoth++; else outOfDomainAny++;
    if(!(a >= domainStart && a<domainEnd) && !(b >= domainStart && b<domainEnd)) outOfDomainBoth++;
   }
  }
  log(strD(outOfDomainA, outOfDomainA+inDomainA),
        strD(outOfDomainB, outOfDomainB+inDomainB),
        strD(outOfDomainAny, outOfDomainAny+inDomainBoth));*/
 }
 }
#endif
  grainGrainSumTime.stop();
  grainGrainTotalTime.stop();
}
