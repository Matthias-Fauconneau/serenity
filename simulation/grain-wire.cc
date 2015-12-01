// TODO: Cylinder contacts
#include "simulation.h"
#include "parallel.h"
#include "disasm.h"

static inline void evaluateGrainWire(const size_t start, const size_t size,
                                     const uint* grainWireContact,
                                     const uint* grainWireA, const uint* grainWireB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const float* wirePx, const float* wirePy, const float* wirePz,
                                     const v8sf Gr_Wr, const v8sf Gr,
                                     float* const grainWireLocalAx, float* const grainWireLocalAy, float* const grainWireLocalAz,
                                     float* const grainWireLocalBx, float* const grainWireLocalBy, float* const grainWireLocalBz,
                                     const v8sf K, const v8sf Kb,
                                     const v8sf staticFrictionStiffness, const v8sf dynamicFrictionCoefficient,
                                     const float staticFrictionLength, const float staticFrictionSpeed,
                                     const float staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                     const vec4* Arotation,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const v8ui contacts = *(v8ui*)(grainWireContact+i);
  const v8ui A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const v8sf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const v8sf Bx = gather(wirePx, B), By = gather(wirePy, B), Bz = gather(wirePz, B);
  const v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  const v8sf depth = Gr_Wr - length;
  const v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const v8sf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Wire

  // Tension

  const v8sf fK = K * sqrt8(depth) * depth;

  // Relative velocity
  v8sf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  v8sf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - gather(BVx, B);
  v8sf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - gather(BVy, B);
  v8sf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - gather(BVz, B);

  // Damping
  v8sf normalSpeed = Nx*RVx+Ny*RVy+Nz*RVz;
  v8sf fB = - Kb * sqrt8(depth) * normalSpeed ; // Damping

  v8sf fN = fK+ fB;
  v8sf NFx = fN * Nx;
  v8sf NFy = fN * Ny;
  v8sf NFz = fN * Nz;
  v8sf Fx = NFx;
  v8sf Fy = NFy;
  v8sf Fz = NFz;

  // Gather static frictions
  v8sf localAx = gather(grainWireLocalAx, contacts);
  v8sf localAy = gather(grainWireLocalAy, contacts);
  v8sf localAz = gather(grainWireLocalAz, contacts);
  v8sf localBx = gather(grainWireLocalBx, contacts);
  v8sf localBy = gather(grainWireLocalBy, contacts);
  v8sf localBz = gather(grainWireLocalBz, contacts);

  v8sf FRAx, FRAy, FRAz;
  v8sf FRBx, FRBy, FRBz;
  for(size_t k: range(simd)) { // FIXME
   if(!localAx[k]) {
    vec3 localA = qapply(conjugate(Arotation[A[k]]), vec3(RAx[k], RAy[k], RAz[k]));
    localAx[k] = localA[0];
    localAy[k] = localA[1];
    localAz[k] = localA[2];
   }
   vec3 relativeA = qapply(Arotation[A[k]], vec3(localAx[k], localAy[k], localAz[k]));
   FRAx[k] = relativeA[0];
   FRAy[k] = relativeA[1];
   FRAz[k] = relativeA[2];
   FRBx[k] = localBx[k];
   FRBy[k] = localBy[k];
   FRBz[k] = localBz[k];
  }

  v8sf gAx = Ax + FRAx;
  v8sf gAy = Ay + FRAy;
  v8sf gAz = Az + FRAz;
  v8sf gBx = Bx + FRBx;
  v8sf gBy = By + FRBy;
  v8sf gBz = Bz + FRBz;
  v8sf Dx = gBx - gAx;
  v8sf Dy = gBy - gAy;
  v8sf Dz = gBz - gAz;
  v8sf Dn = Nx*Dx + Ny*Dy + Nz*Dz;
  // tangentOffset
  v8sf TOx = Dx - Dn * Nx;
  v8sf TOy = Dy - Dn * Ny;
  v8sf TOz = Dz - Dn * Nz;
  v8sf tangentLength = sqrt8(TOx*TOx+TOy*TOy+TOz*TOz);
  v8sf kS = staticFrictionStiffness * fN;
  v8sf fS = kS * tangentLength; // 0.1~1 fN

  // tangentRelativeVelocity
  v8sf RVn = Nx*RVx + Ny*RVy + Nz*RVz;
  v8sf TRVx = RVx - RVn * Nx;
  v8sf TRVy = RVy - RVn * Ny;
  v8sf TRVz = RVz - RVn * Nz;
  v8sf tangentRelativeSpeed = sqrt8(TRVx*TRVx + TRVy*TRVy + TRVz*TRVz);
  v8sf fD = dynamicFrictionCoefficient * fN;
  v8sf fTx, fTy, fTz;
  for(size_t k: range(simd)) { // FIXME: mask
   fTx[k] = 0;
   fTy[k] = 0;
   fTz[k] = 0;
   /*assert_(tangentLength[k] < staticFrictionLength || !isNumber(tangentLength[k]),
             tangentLength[k], staticFrictionLength);*/
   if(      tangentLength[k] < staticFrictionLength
            && tangentRelativeSpeed[0] < staticFrictionSpeed
            //&& fS[k] < fD[k]
            ) {
    // Static
    if(tangentLength[k]) {
     vec3 springDirection = vec3(TOx[k], TOy[k], TOz[k]) / tangentLength[k];
     float fB = staticFrictionDamping * dot(springDirection, vec3(RVx[k], RVy[k], RVz[k]));
     fTx[k] = (fS[k]-fB) * springDirection[0];
     fTy[k] = (fS[k]-fB) * springDirection[1];
     fTz[k] = (fS[k]-fB) * springDirection[2];
    }
   } else { // 0
    localAx[k] = 0; localAy[k] = 0, localAz[k] = 0; localBx[k] = 0, localBy[k] = 0, localBz[k] = 0;
   }
   if(tangentRelativeSpeed[k]) {
    float fDN = - fD[k] / tangentRelativeSpeed[k];
    fTx[k] += fDN * TRVx[k];
    fTy[k] += fDN * TRVy[k];
    fTz[k] += fDN * TRVz[k];
   }
  }
  *(v8sf*)&pFx[i] = Fx + fTx;
  *(v8sf*)&pFy[i] = Fy + fTy;
  *(v8sf*)&pFz[i] = Fz + fTz;
  *(v8sf*)&pTAx[i] = RAy*fTz - RAz*fTy;
  *(v8sf*)&pTAy[i] = RAz*fTx - RAx*fTz;
  *(v8sf*)&pTAz[i] = RAx*fTy - RAy*fTx;
  // Scatter static frictions
  scatter(grainWireLocalAx, contacts, localAx);
  scatter(grainWireLocalAy, contacts, localAy);
  scatter(grainWireLocalAz, contacts, localAz);
  scatter(grainWireLocalBx, contacts, localBx);
  scatter(grainWireLocalBy, contacts, localBy);
  scatter(grainWireLocalBz, contacts, localBz);
 }
}

void Simulation::stepGrainWire() {
 if(!grain.count || !wire.count) return;
 if(grainWireGlobalMinD <= 0)  {
  grainWireSearchTime.start();

  vec3 min, max; domain(min, max);
  Grid grid(1/(Grain::radius+Grain::radius), min, max);
  for(size_t i: range(wire.count))
   grid.cell(wire.Px[i], wire.Py[i], wire.Pz[i]).append(1+i);

  const float verletDistance = 2*(2*Grain::radius/sqrt(3.)) - (Grain::radius + Wire::radius);
  //const float verletDistance = Grain::radius + Grain::radius;
  assert_(verletDistance > Grain::radius + Wire::radius);
  assert_(verletDistance <= Grain::radius + Grain::radius);
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  float minD = inf;

  const int X = grid.size.x, Y = grid.size.y;
  const uint16* wireNeighbours[3*3] = {
   grid.base+(-X*Y-X-1)*Grid::cellCapacity,
   grid.base+(-X*Y-1)*Grid::cellCapacity,
   grid.base+(-X*Y+X-1)*Grid::cellCapacity,

   grid.base+(-X-1)*Grid::cellCapacity,
   grid.base+(-1)*Grid::cellCapacity,
   grid.base+(X-1)*Grid::cellCapacity,

   grid.base+(X*Y-X-1)*Grid::cellCapacity,
   grid.base+(X*Y-1)*Grid::cellCapacity,
   grid.base+(X*Y+X-1)*Grid::cellCapacity
  };

  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageGrainWireContactCount = 16;
  const size_t GWcc = align(simd, grain.count * averageGrainWireContactCount + 1);
  buffer<uint> grainWireA (GWcc, 0);
  buffer<uint> grainWireB (GWcc, 0);
  buffer<float> grainWireLocalAx (GWcc, 0);
  buffer<float> grainWireLocalAy (GWcc, 0);
  buffer<float> grainWireLocalAz (GWcc, 0);
  buffer<float> grainWireLocalBx (GWcc, 0);
  buffer<float> grainWireLocalBy (GWcc, 0);
  buffer<float> grainWireLocalBz (GWcc, 0);

  size_t grainWireIndex = 0; // Index of first contact with A in old grainWire[Local]A|B list
  for(size_t a: range(grain.count)) { // TODO: SIMD
   size_t offset = grid.index(grain.Px[a], grain.Py[a], grain.Pz[a]);
   // Neighbours
   for(size_t n: range(3*3)) for(size_t i: range(3)) {
    ref<uint16> list(wireNeighbours[n] + offset + i * Grid::cellCapacity, Grid::cellCapacity);

    for(size_t j: range(Grid::cellCapacity)) {
     assert_(list.begin() >= grid.cells.begin() && list.end()<=grid.cells.end(), offset, n, i);
     size_t b = list[j];
     if(!b) break;
     b--;
     assert_(a < grain.count && b < wire.count, a, grain.count, b, wire.count, offset, n, i);
     float d = sqrt(sq(grain.Px[a]-wire.Px[b])
                    + sq(grain.Py[a]-wire.Py[b])
                    + sq(grain.Pz[a]-wire.Pz[b])); // TODO: SIMD //FIXME: fails with Ofast?
     if(d > verletDistance) { minD=::min(minD, d); continue; }
     assert_(grainWireA.size < grainWireA.capacity);
     grainWireA.append( a ); // Grain
     grainWireB.append( b ); // Wire
     for(size_t k = 0;; k++) {
      size_t j = grainWireIndex+k;
      if(j >= this->grainWireA.size || this->grainWireA[grainWireIndex+k] != a) break;
      if(this->grainWireB[j] == b) { // Repack existing friction
       grainWireLocalAx.append( this->grainWireLocalAx[j] );
       grainWireLocalAy.append( this->grainWireLocalAy[j] );
       grainWireLocalAz.append( this->grainWireLocalAz[j] );
       grainWireLocalBx.append( this->grainWireLocalBx[j] );
       grainWireLocalBy.append( this->grainWireLocalBy[j] );
       grainWireLocalBz.append( this->grainWireLocalBz[j] );
       goto break_;
      }
     } /*else*/ { // New contact
      // Appends zero to reserve slot. Zero flags contacts for evaluation.
      // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
      grainWireLocalAx.append( 0 );
      grainWireLocalAy.append( 0 );
      grainWireLocalAz.append( 0 );
      grainWireLocalBx.append( 0 );
      grainWireLocalBy.append( 0 );
      grainWireLocalBz.append( 0 );
     }
break_:;
    }
    while(grainWireIndex < this->grainWireA.size && this->grainWireA[grainWireIndex] == a)
     grainWireIndex++;
   }
  }

  assert_(align(simd, grainWireA.size+1) <= grainWireA.capacity);
  for(size_t i=grainWireA.size; i<align(simd, grainWireA.size+1); i++) grainWireA.begin()[i] = 0;
  this->grainWireA = move(grainWireA);
  assert_(align(simd, grainWireB.size+1) <= grainWireB.capacity);
  for(size_t i=grainWireB.size; i<align(simd, grainWireB.size+1); i++) grainWireB.begin()[i] = 0;
  this->grainWireB = move(grainWireB);
  this->grainWireLocalAx = move(grainWireLocalAx);
  this->grainWireLocalAy = move(grainWireLocalAy);
  this->grainWireLocalAz = move(grainWireLocalAz);
  this->grainWireLocalBx = move(grainWireLocalBx);
  this->grainWireLocalBy = move(grainWireLocalBy);
  this->grainWireLocalBz = move(grainWireLocalBz);

  grainWireGlobalMinD = minD - (Grain::radius+Wire::radius);
  if(grainWireGlobalMinD < 0) log("grainWireGlobalMinD", grainWireGlobalMinD);

  grainWireSearchTime.stop();
  if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-wire", grainWireSkipped);
  grainWireSkipped=0;
 } else grainWireSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 grainWireFilterTime.start();
 buffer<uint> grainWireContact(align(simd, grainWireA.size), 0);
 for(size_t index = 0; index < grainWireA.size; index += 8) {
  v8ui A = *(v8ui*)(grainWireA.data+index), B = *(v8ui*)(grainWireB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(wire.Px, B), By = gather(wire.Py, B), Bz = gather(wire.Pz, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Wire::radius) - length;
  for(size_t k: range(simd)) {
   size_t j = index+k;
   if(j == grainWireA.size) break /*2*/;
   if(depth[k] > 0) {
    // Creates a map from packed contact to index into unpacked contact list (indirect reference)
    // Instead of packing (copying) the unpacked list to a packed contact list
    // To keep track of where to write back (unpacked) contact positions (for static friction)
    // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
    grainWireContact.append( j );
   } else {
    // Resets contact (static friction spring)
    grainWireLocalAx[j] = 0; grainWireLocalAy[j] = 0; grainWireLocalAz[j] = 0;
    grainWireLocalBx[j] = 0; grainWireLocalBy[j] = 0; grainWireLocalBz[j] = 0;
   }
  }
 }
 for(size_t i=grainWireContact.size; i<align(simd, grainWireContact.size); i++)
  grainWireContact.begin()[i] = grainWireA.size;
 grainWireFilterTime.stop();

 // Evaluates forces from (packed) intersections (SoA)
 //grainWireEvaluateTime.start();
 size_t GWcc = align(simd, grainWireContact.size); // Grain-Wire contact count
 buffer<float> Fx(GWcc), Fy(GWcc), Fz(GWcc);
 buffer<float> TAx(GWcc), TAy(GWcc), TAz(GWcc);
 static constexpr float E = 1/(1/Grain::elasticModulus+1/Wire::elasticModulus);
 static constexpr float R = 1/(Grain::curvature+Wire::curvature);
 const float K = 4./3*E*sqrt(R);
 if(0) grainWireEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
   (//disasm(evaluateGrainWire,
    evaluateGrainWire(start, size,
                      grainWireContact.data,
                      grainWireA.data, grainWireB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      wire.Px.data, wire.Py.data, grain.Pz.data,
                      float8(Grain::radius+Wire::radius), float8(Grain::radius),
                      grainWireLocalAx.begin(), grainWireLocalAy.begin(), grainWireLocalAz.begin(),
                      grainWireLocalBx.begin(), grainWireLocalBy.begin(), grainWireLocalBz.begin(),
                      float8(K), float8(K * normalDamping),
                      float8(staticFrictionStiffness), float8(dynamicFrictionCoefficient),
                      staticFrictionLength, staticFrictionSpeed, staticFrictionDamping,
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      wire.Vx.data, wire.Vy.data, wire.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data, grain.rotation.data,
                      Fx.begin(), Fy.begin(), Fz.begin(),
                      TAx.begin(), TAy.begin(), TAz.begin()) );
 });
 grainWireContactSizeSum += grainWireContact.size;
 //grainWireEvaluateTime.stop();

 grainWireSumTime.start();
 for(size_t i = 0; i < grainWireContact.size; i++) { // Scalar scatter add
  size_t index = grainWireContact[i];
  size_t a = grainWireA[index];
  size_t b = grainWireB[index];
  grain.Fx[a] += Fx[i];
  wire .Fx[b] -= Fx[i];
  grain.Fy[a] += Fy[i];
  wire .Fy[b] -= Fy[i];
  grain.Fz[a] += Fz[i];
  wire .Fz[b] -= Fz[i];
  grain.Tx[a] += TAx[i];
  grain.Ty[a] += TAy[i];
  grain.Tz[a] += TAz[i];
 }
 grainWireSumTime.stop();
}
