// TODO: Cylinder contacts
#include "simulation.h"
#include "grid.h"
#include "parallel.h"

static inline void evaluateGrainWire(const size_t start, const size_t size,
                                     const uint* grainWireContact, const size_t unused grainWireContactSize,
                                     const uint* grainWireA, const uint* grainWireB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const float* wirePx, const float* wirePy, const float* wirePz,
                                     const vXsf Gr_Wr, const vXsf Gr, const vXsf Wr,
                                     float* const grainWireLocalAx, float* const grainWireLocalAy, float* const grainWireLocalAz,
                                     float* const grainWireLocalBx, float* const grainWireLocalBy, float* const grainWireLocalBz,
                                     const vXsf K, const vXsf Kb,
                                     const vXsf staticFrictionStiffness, const vXsf dynamicFrictionCoefficient,
                                     const vXsf staticFrictionLength, const vXsf staticFrictionSpeed,
                                     const vXsf staticFrictionDamping,
                                     const float* AVx, const float* AVy, const float* AVz,
                                     const float* BVx, const float* BVy, const float* BVz,
                                     const float* pAAVx, const float* pAAVy, const float* pAAVz,
                                     const float* ArotationX, const float* ArotationY, const float* ArotationZ,
                                     const float* ArotationW,
                                     float* const pFx, float* const pFy, float* const pFz,
                                     float* const pTAx, float* const pTAy, float* const pTAz) {
 for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
  const vXui contacts = *(vXui*)(grainWireContact+i);
  const vXui A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const vXsf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const vXsf Bx = gather(wirePx, B), By = gather(wirePy, B), Bz = gather(wirePz, B);
  const vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const vXsf depth = Gr_Wr - length;
  const vXsf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const vXsf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const vXsf RBx = + Wr  * Nx, RBy = + Wr * Ny, RBz = + Wr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Wire

  // Tension
  const vXsf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const vXsf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const vXsf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - gather(BVx, B);
  const vXsf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - gather(BVy, B);
  const vXsf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - gather(BVz, B);
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
  const vXsf oldLocalAx = gather(grainWireLocalAx, contacts);
  const vXsf oldLocalAy = gather(grainWireLocalAy, contacts);
  const vXsf oldLocalAz = gather(grainWireLocalAz, contacts);
  const vXsf oldLocalBx = gather(grainWireLocalBx, contacts);
  const vXsf oldLocalBy = gather(grainWireLocalBy, contacts);
  const vXsf oldLocalBz = gather(grainWireLocalBz, contacts);

  const vXsf QAx = gather(ArotationX, A);
  const vXsf QAy = gather(ArotationY, A);
  const vXsf QAz = gather(ArotationZ, A);
  const vXsf QAw = gather(ArotationW, A);
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

  vec3 min, max; domain(min, max);
  Grid grid(1/(Grain::radius+Grain::radius), min, max);
  for(size_t i: range(wire.count))
   grid.cell(wire.Px[i], wire.Py[i], wire.Pz[i]).append(1+i);

  const float verletDistance = 2*(2*Grain::radius/sqrt(3.)) - (Grain::radius + Wire::radius);
  //const float verletDistance = Grain::radius + Grain::radius;
  assert_(verletDistance > Grain::radius + Wire::radius);
  assert_(verletDistance <= Grain::radius + Grain::radius);
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  float minD = __builtin_inff();

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

  swap(oldGrainWireA, grainWireA);
  swap(oldGrainWireB, grainWireB);
  swap(oldGrainWireLocalAx, grainWireLocalAx);
  swap(oldGrainWireLocalAy, grainWireLocalAy);
  swap(oldGrainWireLocalAz, grainWireLocalAz);
  swap(oldGrainWireLocalBx, grainWireLocalBx);
  swap(oldGrainWireLocalBy, grainWireLocalBy);
  swap(oldGrainWireLocalBz, grainWireLocalBz);

  static constexpr size_t averageGrainWireContactCount = 32;
  const size_t GWcc = align(simd, grain.count * averageGrainWireContactCount +1);
  if(GWcc > grainWireA.capacity) {
   grainWireA = buffer<uint>(GWcc, 0);
   grainWireB = buffer<uint>(GWcc, 0);
   grainWireLocalAx = buffer<float>(GWcc, 0);
   grainWireLocalAy = buffer<float>(GWcc, 0);
   grainWireLocalAz = buffer<float>(GWcc, 0);
   grainWireLocalBx = buffer<float>(GWcc, 0);
   grainWireLocalBy = buffer<float>(GWcc, 0);
   grainWireLocalBz = buffer<float>(GWcc, 0);
  }
  grainWireA.size = 0;
  grainWireB.size = 0;
  grainWireLocalAx.size = 0;
  grainWireLocalAy.size = 0;
  grainWireLocalAz.size = 0;
  grainWireLocalBx.size = 0;
  grainWireLocalBy.size = 0;
  grainWireLocalBz.size = 0;

  size_t grainWireIndex = 0; // Index of first contact with A in old grainWire[Local]A|B list
  grainWireSearchTime += parallel_chunk(grain.count, [&](uint, size_t start, size_t size) {
   for(size_t a=start; a<(start+size); a+=1) { // TODO: SIMD
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
        if(j >= oldGrainWireA.size || oldGrainWireA[grainWireIndex+k] != a) break;
        if(oldGrainWireB[j] == b) { // Repack existing friction
         grainWireLocalAx.append( oldGrainWireLocalAx[j] );
         grainWireLocalAy.append( oldGrainWireLocalAy[j] );
         grainWireLocalAz.append( oldGrainWireLocalAz[j] );
         grainWireLocalBx.append( oldGrainWireLocalBx[j] );
         grainWireLocalBy.append( oldGrainWireLocalBy[j] );
         grainWireLocalBz.append( oldGrainWireLocalBz[j] );
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
      while(grainWireIndex < oldGrainWireA.size && oldGrainWireA[grainWireIndex] == a)
       grainWireIndex++;
     }
    }
  }, 1);

  assert_(align(simd, grainWireA.size+1) <= grainWireA.capacity);
  for(size_t i=grainWireA.size; i<align(simd, grainWireA.size+1); i++) grainWireA.begin()[i] = 0;
  assert_(align(simd, grainWireB.size+1) <= grainWireB.capacity);
  for(size_t i=grainWireB.size; i<align(simd, grainWireB.size+1); i++) grainWireB.begin()[i] = 0;

  grainWireGlobalMinD = minD - (Grain::radius+Wire::radius);
  if(grainWireGlobalMinD < 0) log("grainWireGlobalMinD", grainWireGlobalMinD);

  /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-wire", grainWireSkipped);*/
  grainWireSkipped=0;
 } else grainWireSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainWireA.size) > grainWireContact.capacity) {
  grainWireContact = buffer<uint>(align(simd, grainWireA.size));
 }
 grainWireContact.size = 0;
 grainWireFilterTime += parallel_chunk(align(simd, grainWireA.size)/simd, [&](uint, size_t start, size_t size) {
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    vXui A = *(vXui*)(grainWireA.data+i), B = *(vXui*)(grainWireB.data+i);
    vXsf Ax = gather(grain.Px.data, A), Ay = gather(grain.Py.data, A), Az = gather(grain.Pz.data, A);
    vXsf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
    vXsf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    vXsf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    vXsf depth = floatX(Grain::radius+Wire::radius) - length;
    for(size_t k: range(simd)) {
     size_t j = i+k;
     if(j == grainWireA.size) break /*2*/;
     if(extract(depth, k) > 0) {
      // Creates a map from packed contact to index into unpacked contact list (indirect reference)
      // Instead of packing (copying) the unpacked list to a packed contact list
      // To keep track of where to write back (unpacked) contact positions (for static friction)
      // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
      grainWireContact.append( j );
     } else {
      // Resets contact (static friction spring)
      grainWireLocalAx[j] = 0; /*grainWireLocalAy[j] = 0; grainWireLocalAz[j] = 0;
      grainWireLocalBx[j] = 0; grainWireLocalBy[j] = 0; grainWireLocalBz[j] = 0;*/
     }
    }
   }
 }, 1);
 for(size_t i=grainWireContact.size; i<align(simd, grainWireContact.size); i++)
  grainWireContact.begin()[i] = grainWireA.size;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, grainWireContact.size); // Grain-Wire contact count
 if(GWcc > grainWireFx.capacity) {
  grainWireFx = buffer<float>(GWcc);
  grainWireFy = buffer<float>(GWcc);
  grainWireFz = buffer<float>(GWcc);
  grainWireTAx = buffer<float>(GWcc);
  grainWireTAy = buffer<float>(GWcc);
  grainWireTAz = buffer<float>(GWcc);
 }
 grainWireFx.size = GWcc;
 grainWireFy.size = GWcc;
 grainWireFz.size = GWcc;
 grainWireTAx.size = GWcc;
 grainWireTAy.size = GWcc;
 grainWireTAz.size = GWcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Grain::poissonRatio))/Grain::elasticModulus);
 constexpr float R = 1/(Grain::curvature+Wire::curvature);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass+1/Wire::mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainWireEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainWire(start, size,
                      grainWireContact.data, grainWireContact.size,
                      grainWireA.data, grainWireB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      wire.Px.data, wire.Py.data, wire.Pz.data,
                      floatX(Grain::radius+Wire::radius), floatX(Grain::radius), floatX(Wire::radius),
                      grainWireLocalAx.begin(), grainWireLocalAy.begin(), grainWireLocalAz.begin(),
                      grainWireLocalBx.begin(), grainWireLocalBy.begin(), grainWireLocalBz.begin(),
                      floatX(K), floatX(Kb),
                      floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                      floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      wire.Vx.data, wire.Vy.data, wire.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grainWireFx.begin(), grainWireFy.begin(), grainWireFz.begin(),
                      grainWireTAx.begin(), grainWireTAy.begin(), grainWireTAz.begin() );
 });
 grainWireContactSizeSum += grainWireContact.size;

 grainWireSumTime.start();
 for(size_t i = 0; i < grainWireContact.size; i++) { // Scalar scatter add
  size_t index = grainWireContact[i];
  size_t a = grainWireA[index];
  size_t b = grainWireB[index];
  grain.Fx[a] += grainWireFx[i];
  wire .Fx[b] -= grainWireFx[i];
  grain.Fy[a] += grainWireFy[i];
  wire .Fy[b] -= grainWireFy[i];
  grain.Fz[a] += grainWireFz[i];
  wire .Fz[b] -= grainWireFz[i];
  grain.Tx[a] += grainWireTAx[i];
  grain.Ty[a] += grainWireTAy[i];
  grain.Tz[a] += grainWireTAz[i];
 }
 grainWireSumTime.stop();
}
