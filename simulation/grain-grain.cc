#include "simulation.h"
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
                                     const int* grainGrainContact,
                                     const int* grainGrainA, const int* grainGrainB,
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
  const vXsi contacts = load(grainGrainContact, i);
  const vXsi A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
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

  store(pFx, i, NFx + FTx);
  store(pFy, i, NFy + FTy);
  store(pFz, i, NFz + FTz);
  store(pTAx, i, ARy*FTz - ARz*FTy);
  store(pTAy, i, ARz*FTx - ARx*FTz);
  store(pTAz, i, ARx*FTy - ARy*FTx);
  store(pTBx, i, BRz*FTy - BRy*FTz);
  store(pTBy, i, BRx*FTz - BRz*FTx);
  store(pTBz, i, BRy*FTx - BRx*FTy);
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
  grainLattice();

  swap(oldGrainGrainA, grainGrainA);
  swap(oldGrainGrainB, grainGrainB);
  swap(oldGrainGrainLocalAx, grainGrainLocalAx);
  swap(oldGrainGrainLocalAy, grainGrainLocalAy);
  swap(oldGrainGrainLocalAz, grainGrainLocalAz);
  swap(oldGrainGrainLocalBx, grainGrainLocalBx);
  swap(oldGrainGrainLocalBy, grainGrainLocalBy);
  swap(oldGrainGrainLocalBz, grainGrainLocalBz);

  static constexpr uint averageGrainGrainContactCount = 7;
  uint GGcc = align(simd, grain.count * averageGrainGrainContactCount +1);
  if(GGcc > grainGrainA.capacity) {
   memoryTime.start();
   grainGrainA = buffer<int>(GGcc, 0);
   grainGrainB = buffer<int>(GGcc, 0);
   grainGrainLocalAx = buffer<float>(GGcc, 0);
   grainGrainLocalAy = buffer<float>(GGcc, 0);
   grainGrainLocalAz = buffer<float>(GGcc, 0);
   grainGrainLocalBx = buffer<float>(GGcc, 0);
   grainGrainLocalBy = buffer<float>(GGcc, 0);
   grainGrainLocalBz = buffer<float>(GGcc, 0);
   memoryTime.stop();
  }
  grainGrainA.size = grainGrainA.capacity;
  grainGrainB.size = grainGrainB.capacity;

  const float verletDistance = 2*2*Grain::radius/sqrt(3.); // > Grain::radius + Grain::radius
  const int32/*16*/* latticeNeighbours[62];
  {const int Y = lattice.size.y, X = lattice.size.x;
   uint i = 0; for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
    latticeNeighbours[i++] = lattice.base.data + z*Y*X + y*X + x;
   }
  }

  atomic contactCount;
  auto search = [this, &latticeNeighbours, verletDistance, &contactCount](uint, uint start, uint size) {
   const float* const gPx = grain.Px.data+simd, *gPy = grain.Py.data+simd, *gPz = grain.Pz.data+simd;
   int* const ggA = grainGrainA.begin(), *ggB = grainGrainB.begin();
   const vXsf scale = floatX(lattice.scale);
   const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
   const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
   const vXsf verletDistanceX = floatX(verletDistance);
   const vXsi _1i = intX(-1);
   for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsi a = intX(i)+_seqi;
    const vXsf Ax = load(gPx, i), Ay = load(gPy, i), Az = load(gPz, i);
    vXsi index = convert(scale*(Az-minZ)) * sizeYX
                       + convert(scale*(Ay-minY)) * sizeX
                       + convert(scale*(Ax-minX));
    // Neighbours
    for(uint n: range(62)) { // A is not monotonous
     vXsi b = gather(latticeNeighbours[n], index);
     const vXsf Bx = gather(gPx, b), By = gather(gPy, b), Bz = gather(gPz, b);
     const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     vXsf distance = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
     maskX mask = notEqual(b, _1i) & lessThan(distance, verletDistanceX);
     uint targetIndex = contactCount.fetchAdd(countBits(mask));
     compressStore(ggA+targetIndex, mask, a);
     compressStore(ggB+targetIndex, mask, b);
    }
   }
  };
  if(grain.count/simd) grainGrainSearchTime += parallel_chunk(grain.count/simd, search);
  if(grain.count%simd) {
   const float* const gPx = grain.Px.data+simd, *gPy = grain.Py.data+simd, *gPz = grain.Pz.data+simd;
   int* const ggA = grainGrainA.begin(), *ggB = grainGrainB.begin();
   const vXsf scale = floatX(lattice.scale);
   const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
   const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
   const vXsf verletDistanceX = floatX(verletDistance);
   const vXsi _1i = intX(-1);
   uint i=grain.count/simd*simd;
   vXsi a = intX(i)+_seqi;
   maskX valid = lessThan(a, intX(grain.count));
   const vXsf Ax = load(gPx, i), Ay = load(gPy, i), Az = load(gPz, i);
   vXsi index = convert(scale*(Az-minZ)) * sizeYX
     + convert(scale*(Ay-minY)) * sizeX
     + convert(scale*(Ax-minX));
   for(int k: range(grain.count-i, simd)) insert(index, k, 0);
   // Neighbours
   for(uint n: range(62)) { // A is not monotonous
    vXsi b = gather(latticeNeighbours[n], index);
    const vXsf Bx = gather(gPx, b), By = gather(gPy, b), Bz = gather(gPz, b);
    const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf distance = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    maskX mask = valid & notEqual(b, _1i) & lessThan(distance, verletDistanceX);
    uint targetIndex = contactCount.fetchAdd(countBits(mask));
    compressStore(ggA+targetIndex, mask, a);
    compressStore(ggB+targetIndex, mask, b);
   }
  }
  if(!contactCount) return;
  assert(contactCount <= grainGrainA.capacity);
  grainGrainA.size = contactCount;
  grainGrainB.size = contactCount;
  grainGrainLocalAx.size = contactCount;
  grainGrainLocalAy.size = contactCount;
  grainGrainLocalAz.size = contactCount;
  grainGrainLocalBx.size = contactCount;
  grainGrainLocalBy.size = contactCount;
  grainGrainLocalBz.size = contactCount;

  grainGrainRepackFrictionTime.start();
  uint grainGrainIndex = 0; // Index of first contact with A in old grainGrain[Local]A|B list
  for(uint i=0; i<grainGrainA.size; i++) { // seq
   int a = grainGrainA[i];
   int b = grainGrainB[i];
   assert(a != b);
   for(uint k = 0;; k++) {
    uint j = grainGrainIndex+k;
    if(j >= oldGrainGrainA.size || oldGrainGrainA[j] != a) break;
    if(oldGrainGrainB[j] == b) { // Repack existing friction
     grainGrainLocalAx[i] = oldGrainGrainLocalAx[j];
     grainGrainLocalAy[i] = oldGrainGrainLocalAy[j];
     grainGrainLocalAz[i] = oldGrainGrainLocalAz[j];
     grainGrainLocalBx[i] = oldGrainGrainLocalBx[j];
     grainGrainLocalBy[i] = oldGrainGrainLocalBy[j];
     grainGrainLocalBz[i] = oldGrainGrainLocalBz[j];
     goto break_;
    }
   } /*else*/ { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainGrainLocalAx[i] = 0;
   }
   /**/break_:;
   while(grainGrainIndex < oldGrainGrainA.size && oldGrainGrainA[grainGrainIndex] == a)
    grainGrainIndex++;
  }
  grainGrainRepackFrictionTime.stop();

  assert(align(simd, grainGrainA.size+1) <= grainGrainA.capacity);
  for(uint i=grainGrainA.size; i<align(simd, grainGrainA.size+1); i++) grainGrainA.begin()[i] = grain.count;
  for(uint i=grainGrainB.size; i<align(simd, grainGrainB.size+1); i++) grainGrainB.begin()[i] = grain.count;

  grainGrainGlobalMinD = verletDistance/*minD*/ - 2*Grain::radius;
  if(grainGrainGlobalMinD <= 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD);

  //log("grain-grain", grainGrainSkipped);
  grainGrainSkipped=0;
 } else grainGrainSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainGrainA.size) > grainGrainContact.capacity) {
  memoryTime.start();
  grainGrainContact = buffer<int>(align(simd, grainGrainA.size));
  memoryTime.stop();
 }
 grainGrainContact.size = 0;

 // Creates a map from packed contact to index into unpacked contact list (indirect reference)
 // Instead of packing (copying) the unpacked list to a packed contact list
 // To keep track of where to write back (unpacked) contact positions (for static friction)
 // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
 atomic contactCount;
 auto filter = [&](uint, uint start, uint size) {
  const int* const ggA = grainGrainA.data, *ggB = grainGrainB.data;
  const float* const gPx = grain.Px.data+simd, *gPy = grain.Py.data+simd, *gPz = grain.Pz.data+simd;
  float* const ggL = grainGrainLocalAx.begin();
  int* const ggContact = grainGrainContact.begin();
  const vXsf _2Gr = floatX(Grain::radius+Grain::radius);
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsi A = load(ggA, i), B = load(ggB, i);
    vXsf Ax = gather(gPx, A), Ay = gather(gPy, A), Az = gather(gPz, A);
    vXsf Bx = gather(gPx, B), By = gather(gPy, B), Bz = gather(gPz, B);
    vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    vXsf depth = _2Gr - length;
    maskX contact = greaterThanOrEqual(depth, _0f);
    maskStore(ggL+i, ~contact, _0f);
    uint index = contactCount.fetchAdd(countBits(contact));
    compressStore(ggContact+index, contact, intX(i)+_seqi);
  }
 };
 if(grainGrainA.size/simd) grainGrainFilterTime += parallel_chunk(grainGrainA.size/simd, filter);
 // The partial iteration has to be executed last so that invalid contacts are trailing
 // and can be easily trimmed
 if(grainGrainA.size%simd != 0) filter(0, grainGrainA.size/simd, 1u);
 grainGrainContact.size = contactCount;
 while(contactCount.count > 0 && grainGrainContact[contactCount.count-1] >= (int)grainGrainA.size)
  contactCount.count--; // Trims trailing invalid contacts
 grainGrainContact.size = contactCount;
 if(!grainGrainContact.size) return;
 for(uint i=grainGrainContact.size; i<align(simd, grainGrainContact.size); i++)
  grainGrainContact.begin()[i] = grainGrainA.size;;

 // Evaluates forces from (packed) intersections (SoA)
 uint GGcc = align(simd, grainGrainContact.size); // Grain-Grain contact count
 if(GGcc > grainGrainFx.capacity) {
  memoryTime.start();
  grainGrainFx = buffer<float>(GGcc);
  grainGrainFy = buffer<float>(GGcc);
  grainGrainFz = buffer<float>(GGcc);
  grainGrainTAx = buffer<float>(GGcc);
  grainGrainTAy = buffer<float>(GGcc);
  grainGrainTAz = buffer<float>(GGcc);
  grainGrainTBx = buffer<float>(GGcc);
  grainGrainTBy = buffer<float>(GGcc);
  grainGrainTBz = buffer<float>(GGcc);
  memoryTime.stop();
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
 auto evaluate = [this, K, Kb](uint, int start, int size) {
  evaluateGrainGrain(start, size,
                     grainGrainContact.data,
                     grainGrainA.data, grainGrainB.data,
                     grain.Px.data+simd, grain.Py.data+simd, grain.Pz.data+simd,
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
 };
 grainGrainEvaluateTime += parallel_chunk(GGcc/simd, evaluate);

 const uint threadCount = ::min(::threadCount()/2, int(sqrt(grainGrainContact.size/simd)));
 if(threadCount < 4) { // Sequential scalar scatter add
  grainGrainSumTime.start();
  for(int i = 0; i < (int)grainGrainContact.size; i++) {
   int index = grainGrainContact[i];
   int a = grainGrainA[index];
   int b = grainGrainB[index];
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
 } else {
#if __MIC__ || !__INTEL_COMPILER
  const uint jobCount = grainGrainContact.size/simd;
  const uint chunkSize = (jobCount+threadCount-1)/threadCount;
  const uint chunkCount = (jobCount+chunkSize-1)/chunkSize;
  assert(chunkCount <= threadCount);
  uint chunkDomainStarts[chunkCount], chunkDomainStops[chunkCount];
  // Evaluates domain (range) for each chunk
  auto domainMinMax = [&](uint, uint chunkIndex) {
   const int* const grainGrainContact = this->grainGrainContact.data;
   const int* const grainGrainA = this->grainGrainA.data;
   const int* const grainGrainB = this->grainGrainB.data;
   vXsi start = intX(grain.count);
   vXsi stop = _0i;
   for(uint i = chunkIndex*chunkSize*simd; i < min(jobCount, (chunkIndex+1)*chunkSize)*simd; i+=simd) {
    const vXsi contacts = *(vXsi*)(grainGrainContact+i);
    const vXsi A = gather(grainGrainA, contacts);
    const vXsi B = gather(grainGrainB, contacts);
    start = min(start, min(A, B));
    stop = max(stop, max(A, B));
   }
   chunkDomainStarts[chunkIndex] = min(start);
   chunkDomainStops[chunkIndex] = max(stop);
  };
  grainGrainSumDomainTime += parallel_for(0, chunkCount, domainMinMax);
  grainGrainSumAllocateTime.start();
  // Distributes chunks across copies
  uint maxCopyCount = chunkCount, maxDomainCount = chunkCount;
  buffer<uint> copyStart(maxCopyCount*maxDomainCount), copyStop(maxCopyCount*maxDomainCount);
  /*mref<uint>(copyStart, maxCopyCount*maxDomainCount).clear(0);
  mref<uint>(copyStop, maxCopyCount*maxDomainCount).clear(0);*/
  copyStart.clear(0); copyStop.clear(0);
  uint copy[maxCopyCount]; // Allocates buffer copies to chunks without range overlap
  uint copyCount = 0;
  for(uint chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++) {
   uint chunkDomainStart = chunkDomainStarts[chunkIndex];
   uint chunkDomainStop = chunkDomainStops[chunkIndex];
   uint copyIndex = 0;
   for(;; copyIndex++) {
    assert(copyIndex < maxCopyCount, copyIndex, maxCopyCount, chunkIndex);
    for(uint domainIndex = 0;; domainIndex++) {
     assert(domainIndex < maxDomainCount);
     uint domainStart = copyStart[copyIndex*maxDomainCount+domainIndex];
     uint domainStop = copyStop[copyIndex*maxDomainCount+domainIndex];
     if(domainStart == domainStop) break;
     if(chunkDomainStart < domainStop && chunkDomainStop > domainStart) {
      goto continue_2;
     }
    } /*else*/ {
     copyCount = max(copyCount, copyIndex+1);
     copy[chunkIndex] = copyIndex;
     for(uint domainIndex = 0;; domainIndex++) {
      assert(domainIndex < maxDomainCount, domainIndex, maxDomainCount);
      uint& domainStart = copyStart[copyIndex*maxDomainCount+domainIndex];
      uint& domainStop = copyStop[copyIndex*maxDomainCount+domainIndex];
      if(domainStart == domainStop) {
       domainStart = chunkDomainStart;
       domainStop = chunkDomainStop;
       break;
      }
     }
     break; // next chunk
    }
    /**/continue_2:;
   }
   assert(copy[chunkIndex] != ~0u);
  }
  grainGrainSumAllocateTime.stop();
  auto sum = [&](uint, uint chunkIndex) {
   const int* const grainGrainContact = this->grainGrainContact.data;
   const int* const grainGrainA = this->grainGrainA.data;
   const int* const grainGrainB = this->grainGrainB.data;
   const uint stride = align(simd, grain.count);
   const uint base  = copy[chunkIndex]*stride;
   const float* const pGGFx = grainGrainFx.data, *pGGFy = grainGrainFy.data, *pGGFz = grainGrainFz.data;
   const float* const pGGTAx = grainGrainTAx.data, *pGGTAy = grainGrainTAy.data, *pGGTAz = grainGrainTAz.data;
   const float* const pGGTBx = grainGrainTBx.data, *pGGTBy = grainGrainTBy.data, *pGGTBz = grainGrainTBz.data;
   float* const pFx = grain.Fx.begin()+base, *pFy = grain.Fy.begin()+base, *pFz = grain.Fz.begin()+base;
   float* const pTx = grain.Tx.begin()+base, *pTy = grain.Ty.begin()+base, *pTz = grain.Tz.begin()+base;
   // Scalar scatter add
   for(uint i=chunkIndex*chunkSize*simd; i<min(jobCount, (chunkIndex+1)*chunkSize)*simd; i++) {
    uint index = grainGrainContact[i];
    uint a = grainGrainA[index];
    uint b = grainGrainB[index];
    pFx[a] += pGGFx[i];
    pFx[b] -= pGGFx[i];
    pFy[a] += pGGFy[i];
    pFy[b] -= pGGFy[i];
    pFz[a] += pGGFz[i];
    pFz[b] -= pGGFz[i];

    pTx[a] += pGGTAx[i];
    pTy[a] += pGGTAy[i];
    pTz[a] += pGGTAz[i];
    pTx[b] += pGGTBx[i];
    pTy[b] += pGGTBy[i];
    pTz[b] += pGGTBz[i];
   }
  };
  grainGrainSumSumTime += parallel_for(0, chunkCount, sum);
  // TODO: range optimized zero/copy
  auto merge = [this, copyCount](uint, uint start, uint size) {
   float* const pFx = grain.Fx.begin(), *pFy = grain.Fy.begin(), *pFz = grain.Fz.begin();
   float* const pTx = grain.Tx.begin(), *pTy = grain.Ty.begin(), *pTz = grain.Tz.begin();
   const uint stride = align(simd, grain.count);
   for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsf Fx = _0f, Fy = _0f, Fz = _0f, Tx = _0f, Ty = _0f, Tz = _0f;
    for(uint copyIndex = 1; copyIndex < copyCount; copyIndex++) {
     const uint base = copyIndex*stride;
     Fx += load(pFx, base+i); store(pFx, base+i, _0f); // Zeroes for next time
     Fy += load(pFy, base+i); store(pFy, base+i, _0f);
     Fz += load(pFz, base+i); store(pFz, base+i, _0f);
     Tx += load(pTx, base+i); store(pTx, base+i, _0f);
     Ty += load(pTy, base+i); store(pTy, base+i, _0f);
     Tz += load(pTz, base+i); store(pTz, base+i, _0f);
    }
    store(pFx, i, load(pFx, i) + Fx);
    store(pFy, i, load(pFy, i) + Fy);
    store(pFz, i, load(pFz, i) + Fz);
    store(pTx, i, load(pTx, i) + Tx);
    store(pTy, i, load(pTy, i) + Ty);
    store(pTz, i, load(pTz, i) + Tz);
   }
  };
  grainGrainSumMergeTime += parallel_chunk(align(simd, grain.count)/simd, merge);
#else
  error("Unsupported");
#endif
 }
}
