// TODO: Cylinder contacts
#include "simulation.h"
#include "parallel.h"
#include "wire.h"
#include "grain.h"

static inline void evaluateWireGrain(const int start, const int size,
                                     const int* wireGrainContact, const int unused wireGrainContactSize,
                                     const int* wireGrainA, const int* wireGrainB,
                                     const float* wirePx, const float* wirePy, const float* wirePz,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const vXsf Wr_Gr, const vXsf Wr, const vXsf Gr,
                                     float* const wireGrainLocalAx, float* const wireGrainLocalAy, float* const wireGrainLocalAz,
                                     float* const wireGrainLocalBx, float* const wireGrainLocalBy, float* const wireGrainLocalBz,
                                     const vXsf K, const vXsf Kb,
                                     const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                     const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                     const vXsf staticFrictionDamping,
                                     const float* wVx, const float* wVy, const float* wVz,
                                     const float* gVx, const float* gVy, const float* gVz,
                                     const float* pgAVx, const float* pgAVy, const float* pgAVz,
                                     const float* GrotationX, const float* GrotationY, const float* GrotationZ,
                                     const float* GrotationW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pgTx, float* const pgTy, float* const pgTz) {
 for(int i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXsi contacts = *(vXsi*)(wireGrainContact+i);
  const vXsi A = gather(wireGrainA, contacts), B = gather(wireGrainB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(wirePx, A), Ay = gather(wirePy, A), Az = gather(wirePz, A);
  const vXsf Bx = gather(grainPx, B), By = gather(grainPy, B), Bz = gather(grainPz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf L = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Wr_Gr - L;
  const vXsf Nx = Rx/L , Ny = Ry/L , Nz = Rz/L ;
  const vXsf RAx = - Wr  * Nx, RAy = - Wr * Ny, RAz = - Wr * Nz;
  const vXsf RBx = + Gr  * Nx, RBy = + Gr * Ny, RBz = + Gr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Wire

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf BAVx = gather(pgAVx, B), BAVy = gather(pgAVy, B), BAVz = gather(pgAVz, B);
  const vXsf RVx = gather(wVx, A) - gather(gVx, B) - (BAVy*RBz - BAVz*RBy) ;
  const vXsf RVy = gather(wVy, A) - gather(gVy, B) - (BAVz*RBx - BAVx*RBz);
  const vXsf RVz = gather(wVz, A) - gather(gVz, B) - (BAVx*RBy - BAVy*RBx);
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
  const vXsf oldLocalAx = gather(wireGrainLocalAx, contacts);
  const vXsf oldLocalAy = gather(wireGrainLocalAy, contacts);
  const vXsf oldLocalAz = gather(wireGrainLocalAz, contacts);
  const vXsf oldLocalBx = gather(wireGrainLocalBx, contacts);
  const vXsf oldLocalBy = gather(wireGrainLocalBy, contacts);
  const vXsf oldLocalBz = gather(wireGrainLocalBz, contacts);

  const vXsf QBx = gather(GrotationX, B);
  const vXsf QBy = gather(GrotationY, B);
  const vXsf QBz = gather(GrotationZ, B);
  const vXsf QBw = gather(GrotationW, B);
  const vXsf X1 = QBw*RBx + RBy*QBz - QBy*RBz;
  const vXsf Y1 = QBw*RBy + RBz*QBx - QBz*RBx;
  const vXsf Z1 = QBw*RBz + RBx*QBy - QBx*RBy;
  const vXsf W1 = - (RBx * QBx + RBy * QBy + RBz * QBz);
  const vXsf newLocalBx = QBw*X1 - (W1*QBx + QBy*Z1 - Y1*QBz);
  const vXsf newLocalBy = QBw*Y1 - (W1*QBy + QBz*X1 - Z1*QBx);
  const vXsf newLocalBz = QBw*Z1 - (W1*QBz + QBx*Y1 - X1*QBy);

  const vXsf newLocalAx = RAx;
  const vXsf newLocalAy = RAy;
  const vXsf newLocalAz = RAz;

  const maskX reset = equal(oldLocalAx, _0f);
  vXsf localAx = blend(reset, oldLocalAx, newLocalAx);
  const vXsf localAy = blend(reset, oldLocalAy, newLocalAy);
  const vXsf localAz = blend(reset, oldLocalAz, newLocalAz);
  const vXsf localBx = blend(reset, oldLocalBx, newLocalBx);
  const vXsf localBy = blend(reset, oldLocalBy, newLocalBy);
  const vXsf localBz = blend(reset, oldLocalBz, newLocalBz);

  const vXsf X = QBw*localBx - (localBy*QBz - QBy*localBz);
  const vXsf Y = QBw*localBy - (localBz*QBx - QBz*localBx);
  const vXsf Z = QBw*localBz - (localBx*QBy - QBx*localBy);
  const vXsf W = localBx * QBx + localBy * QBy + localBz * QBz;
  const vXsf FRBx = QBw*X + W*QBx + QBy*Z - Y*QBz;
  const vXsf FRBy = QBw*Y + W*QBy + QBz*X - Z*QBx;
  const vXsf FRBz = QBw*Z + W*QBz + QBx*Y - X*QBy;
  const vXsf FRAx = localAx;
  const vXsf FRAy = localAy;
  const vXsf FRAz = localAz;

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
  /*if(0) for(size_t k: range(simd)) {
   if(i+k >= (size_t)wireGrainContactSize) break;
   if(!(::length(vec3(pFx[i+k],pFy[i+k],pFz[i+k])) < 400*N)) {
    cylinders.append(i+k);
    log(length(vec3(pFx[i+k],pFy[i+k],pFz[i+k]))/N, "S0","\n",
      "N", Fn[k],
      "T", length(vec3(FTx[k],FTy[k],FTz[k])));
    fail = true;
    return;
   }
  }*/
  store(pgTx, i, RBz*FTy - RBy*FTz);
  store(pgTy, i, RBx*FTz - RBz*FTx);
  store(pgTz, i, RBy*FTx - RBx*FTy);
  // Scatter static frictions
  scatter(wireGrainLocalAx, contacts, localAx);
  scatter(wireGrainLocalAy, contacts, localAy);
  scatter(wireGrainLocalAz, contacts, localAz);
  scatter(wireGrainLocalBx, contacts, localBx);
  scatter(wireGrainLocalBy, contacts, localBy);
  scatter(wireGrainLocalBz, contacts, localBz);
 }
}

void Simulation::stepWireGrain() {
 if(!grain->count || !wire->count) return;
 if(wireGrainGlobalMinD <= 0)  {
  grainLattice();

  const float verletDistance = 2*(2*grain->radius/sqrt(3.)) - (grain->radius + Wire::radius); // FIXME: ?
  assert(grain->radius + Wire::radius < verletDistance
                                                        && verletDistance <= grain->radius + grain->radius);

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

  swap(oldWireGrainA, wireGrainA);
  swap(oldWireGrainB, wireGrainB);
  swap(oldWireGrainLocalAx, wireGrainLocalAx);
  swap(oldWireGrainLocalAy, wireGrainLocalAy);
  swap(oldWireGrainLocalAz, wireGrainLocalAz);
  swap(oldWireGrainLocalBx, wireGrainLocalBx);
  swap(oldWireGrainLocalBy, wireGrainLocalBy);
  swap(oldWireGrainLocalBz, wireGrainLocalBz);

  static constexpr size_t averageWireGrainContactCount = 16;
  const size_t GWcc = max(4096u, align(simd, grain->count * averageWireGrainContactCount +1));
  if(GWcc > wireGrainA.capacity) {
   wireGrainA = buffer<int>(GWcc, 0);
   wireGrainB = buffer<int>(GWcc, 0);
   wireGrainLocalAx = buffer<float>(GWcc, 0);
   wireGrainLocalAy = buffer<float>(GWcc, 0);
   wireGrainLocalAz = buffer<float>(GWcc, 0);
   wireGrainLocalBx = buffer<float>(GWcc, 0);
   wireGrainLocalBy = buffer<float>(GWcc, 0);
   wireGrainLocalBz = buffer<float>(GWcc, 0);
  }
  wireGrainA.size = wireGrainA.capacity;
  wireGrainB.size = wireGrainB.capacity;

  atomic contactCount;
  auto search = [this, &latticeNeighbours, verletDistance, &contactCount](uint, uint start, uint size) {
    const float* const wPx = wire->Px.data, *wPy = wire->Py.data, *wPz = wire->Pz.data;
    const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
    int* const wgA = wireGrainA.begin(), *wgB = wireGrainB.begin();
    const vXsf scale = floatX(lattice.scale);
    const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
    const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
    const vXsf sqVerletDistanceX = floatX(sq(verletDistance));
    const vXsi _1i = intX(-1);
    for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     const vXsi a = intX(i)+_seqi;
     const vXsf Ax = load(wPx, i), Ay = load(wPy, i), Az = load(wPz, i);
     vXsi index = convert(scale*(Az-minZ)) * sizeYX
                        + convert(scale*(Ay-minY)) * sizeX
                        + convert(scale*(Ax-minX));
     for(int n: range(3*3)) for(int i: range(3)) {
      /*if(1) for(int k: range(simd)) {
       if(!((latticeNeighbours[n]-lattice.base.data)+index[k]+i >= -(lattice.base.data-lattice.cells.data)
            && (latticeNeighbours[n]-lattice.base.data)+index[k]+i<int(lattice.base.size))) {
        log(vec3(minX[k], minY[k], minZ[k]), vec3(Ax[k], Ay[k], Az[k]), lattice.max);
        fail = true;
        return;
       }
      }*/
      const vXsi b = gather(latticeNeighbours[n]+i, index);
      //if(1) for(int k: range(simd)) assert_(b[k] >= -1 && b[k] < grain->count, b[k]);
      const vXsf Bx = gather(gPx, b), By = gather(gPy, b), Bz = gather(gPz, b);
      const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
      vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
      maskX mask = notEqual(b, _1i) & lessThan(sqDistance, sqVerletDistanceX);
      uint targetIndex = contactCount.fetchAdd(countBits(mask));
      compressStore(wgA+targetIndex, mask, a);
      compressStore(wgB+targetIndex, mask, b);
     }
    }
  };
  if(wire->count/simd) wireGrainSearchTime += parallel_chunk(wire->count/simd, search);
  if(fail) return;
  if(wire->count%simd) {
   const float* const wPx = wire->Px.data, *wPy = wire->Py.data, *wPz = wire->Pz.data;
   const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
   int* const wgA = wireGrainA.begin(), *wgB = wireGrainB.begin();
   const vXsf scale = floatX(lattice.scale);
   const vXsf minX = floatX(lattice.min.x), minY = floatX(lattice.min.y), minZ = floatX(lattice.min.z);
   const vXsi sizeX = intX(lattice.size.x), sizeYX = intX(lattice.size.y * lattice.size.x);
   const vXsf sqVerletDistanceX = floatX(sq(verletDistance));
   const vXsi _1i = intX(-1);
   uint i=wire->count/simd*simd;
   const vXsi a = intX(i)+_seqi;
   maskX valid = lessThan(a, intX(wire->count));
   const vXsf Ax = load(wPx, i), Ay = load(wPy, i), Az = load(wPz, i);
   vXsi index = convert(scale*(Az-minZ)) * sizeYX
     + convert(scale*(Ay-minY)) * sizeX
     + convert(scale*(Ax-minX));
   for(int k: range(wire->count-i, simd)) insert(index, k, 0);
   for(int n: range(3*3)) for(int i: range(3)) {
    /*if(1) for(int k: range(simd)) {
     if(!((latticeNeighbours[n]-lattice.base.data)+index[k]+i >= -(lattice.base.data-lattice.cells.data)
          && (latticeNeighbours[n]-lattice.base.data)+index[k]+i<int(lattice.base.size))) {
      log(vec3(minX[k], minY[k], minZ[k]), vec3(Ax[k], Ay[k], Az[k]), lattice.max);
      fail = true;
      return;
     }
    }*/
    const vXsi b = gather(latticeNeighbours[n]+i, index);
    const vXsf Bx = gather(gPx, b), By = gather(gPy, b), Bz = gather(gPz, b);
    const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
    maskX mask = valid & notEqual(b, _1i) & lessThan(sqDistance, sqVerletDistanceX);
    uint targetIndex = contactCount.fetchAdd(countBits(mask));
    compressStore(wgA+targetIndex, mask, a);
    compressStore(wgB+targetIndex, mask, b);
   }
  }
  assert_(contactCount <= wireGrainA.capacity, contactCount.count, wireGrainA.capacity);
  wireGrainA.size = contactCount;
  wireGrainB.size = contactCount;
  wireGrainLocalAx.size = contactCount;
  wireGrainLocalAy.size = contactCount;
  wireGrainLocalAz.size = contactCount;
  wireGrainLocalBx.size = contactCount;
  wireGrainLocalBy.size = contactCount;
  wireGrainLocalBz.size = contactCount;

  assert(align(simd, wireGrainA.size+1) <= wireGrainA.capacity);
  for(size_t i=wireGrainA.size; i<align(simd, wireGrainA.size +1); i++) wireGrainA.begin()[i] = wire->count;
  for(size_t i=wireGrainB.size; i<align(simd, wireGrainB.size +1); i++) wireGrainB.begin()[i] = grain->count;

  wireGrainGlobalMinD = /*minD*/verletDistance - (grain->radius+Wire::radius);
  if(wireGrainGlobalMinD < 0) log("wireGrainGlobalMinD", wireGrainGlobalMinD);

  //log("grain-wire", wireGrainSkipped);
  wireGrainSkipped=0;

  //this->wireGrainSearchTime += wireGrainSearchTime.cycleCount();
  if(!contactCount) return;

  wireGrainRepackTime += parallel_chunk(wireGrainA.size, [&](uint, size_t start, size_t size) {
    for(size_t i=start; i<start+size; i++) {
      int a = wireGrainA[i];
      int b = wireGrainB[i];
      for(int j: range(oldWireGrainA.size)) {
       if(oldWireGrainA[j] == a && oldWireGrainB[j] == b) {
        wireGrainLocalAx[i] = oldWireGrainLocalAx[j];
        wireGrainLocalAy[i] = oldWireGrainLocalAy[j];
        wireGrainLocalAz[i] = oldWireGrainLocalAz[j];
        wireGrainLocalBx[i] = oldWireGrainLocalBx[j];
        wireGrainLocalBy[i] = oldWireGrainLocalBy[j];
        wireGrainLocalBz[i] = oldWireGrainLocalBz[j];
        goto break_;
       }
      } /*else*/ {
       wireGrainLocalAx[i] = 0;
      }
      break_:;
    }
  });
 } else wireGrainSkipped++;

 if(!wireGrainA.size) return;

 // Filters verlet lists, packing contacts to evaluate
 tsc wireGrainFilterTime; wireGrainFilterTime.start();
 if(align(simd, wireGrainA.size) > wireGrainContact.capacity) {
  wireGrainContact = buffer<int>(align(simd, wireGrainA.size));
 }
 wireGrainContact.size = 0;

 atomic contactCount;
 auto filter = [&](uint, size_t start, size_t size) {
  const int* const wgA = wireGrainA.data, *wgB = wireGrainB.data;
  const float* const wPx = wire->Px.data, *wPy = wire->Py.data, *wPz = wire->Pz.data;
  const float* const gPx = grain->Px.data+simd, *gPy = grain->Py.data+simd, *gPz = grain->Pz.data+simd;
  float* const wgL = wireGrainLocalAx.begin();
  int* const wgContact = wireGrainContact.begin();
  const vXsf sq2Radius = floatX(sq(Wire::radius+grain->radius));
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   const vXsi A = load(wgA, i), B = load(wgB, i);
   const vXsf Ax = gather(wPx, A), Ay = gather(wPy, A), Az = gather(wPz, A);
   const vXsf Bx = gather(gPx, B), By = gather(gPy, B), Bz = gather(gPz, B);
   const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   const vXsf sqDistance = Rx*Rx + Ry*Ry + Rz*Rz;
   const maskX contact = lessThan(sqDistance, sq2Radius);
   maskStore(wgL+i, ~contact, _0f);
   const uint index = contactCount.fetchAdd(countBits(contact));
   compressStore(wgContact+index, contact, intX(i)+_seqi);
  }
 };
 if(wireGrainA.size/simd) parallel_chunk(wireGrainA.size/simd, filter);
 if(wireGrainA.size%simd != 0) filter(0, wireGrainA.size/simd, 1u);
 wireGrainContact.size = contactCount;
 while(wireGrainContact.size > 0 && wireGrainContact.last() >= (int)wireGrainA.size)
  wireGrainContact.size--; // Trims trailing invalid contacts
 for(size_t i=wireGrainContact.size; i<align(simd, wireGrainContact.size); i++)
  wireGrainContact.begin()[i] = wireGrainA.size;
 this->wireGrainFilterTime += wireGrainFilterTime.cycleCount();
 if(!wireGrainContact) return;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, wireGrainContact.size); // Grain-Wire contact count
 if(GWcc > wireGrainFx.capacity) {
  wireGrainFx = buffer<float>(GWcc);
  wireGrainFy = buffer<float>(GWcc);
  wireGrainFz = buffer<float>(GWcc);
  wireGrainTBx = buffer<float>(GWcc);
  wireGrainTBy = buffer<float>(GWcc);
  wireGrainTBz = buffer<float>(GWcc);
 }
 wireGrainFx.size = GWcc;
 wireGrainFy.size = GWcc;
 wireGrainFz.size = GWcc;
 wireGrainTBx.size = GWcc;
 wireGrainTBy.size = GWcc;
 wireGrainTBz.size = GWcc;
 const float E = 1/((1-sq(grain->poissonRatio))/grain->elasticModulus+(1-sq(grain->poissonRatio))/grain->elasticModulus);
 const float R = 1/(grain->curvature+Wire::curvature);
 const float K = 4./3*E*sqrt(R);
 const float mass = 1/(1/grain->mass+1/wire->mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 wireGrainEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateWireGrain(start, size,
                      wireGrainContact.data, wireGrainContact.size,
                      wireGrainA.data, wireGrainB.data,
                      wire->Px.data, wire->Py.data, wire->Pz.data,
                      grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd,
                      floatX(Wire::radius+grain->radius), floatX(Wire::radius), floatX(grain->radius),
                      wireGrainLocalAx.begin(), wireGrainLocalAy.begin(), wireGrainLocalAz.begin(),
                      wireGrainLocalBx.begin(), wireGrainLocalBy.begin(), wireGrainLocalBz.begin(),
                      floatX(K), floatX(Kb),
                      floatX(staticFrictionStiffness), floatX(dynamicWireGrainFrictionCoefficient),
                      floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                      wire->Vx.data, wire->Vy.data, wire->Vz.data,
                      grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd,
                      grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd,
                      grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd,
                      wireGrainFx.begin(), wireGrainFy.begin(), wireGrainFz.begin(),
                      wireGrainTBx.begin(), wireGrainTBy.begin(), wireGrainTBz.begin() );
 });
 wireGrainContactSizeSum += wireGrainContact.size;

 wireGrainSumTime.start();
 for(size_t i = 0; i < wireGrainContact.size; i++) { // Scalar scatter add
  size_t index = wireGrainContact[i];
  size_t a = wireGrainA[index];
  size_t b = wireGrainB[index];
  wire->Fx[a] += wireGrainFx[i];
  grain->Fx[simd+b] -= wireGrainFx[i];
  wire->Fy[a] += wireGrainFy[i];
  grain->Fy[simd+b] -= wireGrainFy[i];
  wire->Fz[a] += wireGrainFz[i];
  grain->Fz[simd+b] -= wireGrainFz[i];
  grain->Tx[simd+b] += wireGrainTBx[i];
  grain->Ty[simd+b] += wireGrainTBy[i];
  grain->Tz[simd+b] += wireGrainTBz[i];
 }
 wireGrainSumTime.stop();
}
