// TODO: Face contacts
#include "simulation.h"
#include "parallel.h"

static inline void evaluateGrainMembrane(const size_t start, const size_t size,
                                     const uint* grainMembraneContact, const size_t unused grainMembraneContactSize,
                                     const uint* grainMembraneA, const uint* grainMembraneB,
                                     const float* grainPx, const float* grainPy, const float* grainPz,
                                     const float* membranePx, const float* membranePy, const float* membranePz,
                                     const v8sf Gr_Wr, const v8sf Gr, const v8sf Wr,
                                     float* const grainMembraneLocalAx, float* const grainMembraneLocalAy, float* const grainMembraneLocalAz,
                                     float* const grainMembraneLocalBx, float* const grainMembraneLocalBy, float* const grainMembraneLocalBz,
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
  const v8ui contacts = *(v8ui*)(grainMembraneContact+i);
  const v8ui A = gather(grainMembraneA, contacts), B = gather(grainMembraneB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  const v8sf Ax = gather(grainPx, A), Ay = gather(grainPy, A), Az = gather(grainPz, A);
  const v8sf Bx = gather(membranePx, B), By = gather(membranePy, B), Bz = gather(membranePz, B);
  const v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  const v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
  const v8sf depth = Gr_Wr - length;
  const v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
  const v8sf RAx = - Gr  * Nx, RAy = - Gr * Ny, RAz = - Gr * Nz;
  const v8sf RBx = + Wr  * Nx, RBy = + Wr * Ny, RBz = + Wr * Nz;
  /// Evaluates contact force between two objects with friction (rotating A, non rotating B)
  // Grain - Membrane

  // Tension
  const v8sf Fk = K * sqrt(depth) * depth;
  // Relative velocity
  const v8sf AAVx = gather(pAAVx, A), AAVy = gather(pAAVy, A), AAVz = gather(pAAVz, A);
  const v8sf RVx = gather(AVx, A) + (AAVy*RAz - AAVz*RAy) - gather(BVx, B);
  const v8sf RVy = gather(AVy, A) + (AAVz*RAx - AAVx*RAz) - gather(BVy, B);
  const v8sf RVz = gather(AVz, A) + (AAVx*RAy - AAVy*RAx) - gather(BVz, B);
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
  const v8sf Fd = mask(tangentRelativeSpeed > 0,
                       - dynamicFrictionCoefficient * Fn / tangentRelativeSpeed);
  const v8sf FDx = Fd * TRVx;
  const v8sf FDy = Fd * TRVy;
  const v8sf FDz = Fd * TRVz;

  // Gather static frictions
  const v8sf oldLocalAx = gather(grainMembraneLocalAx, contacts);
  const v8sf oldLocalAy = gather(grainMembraneLocalAy, contacts);
  const v8sf oldLocalAz = gather(grainMembraneLocalAz, contacts);
  const v8sf oldLocalBx = gather(grainMembraneLocalBx, contacts);
  const v8sf oldLocalBy = gather(grainMembraneLocalBy, contacts);
  const v8sf oldLocalBz = gather(grainMembraneLocalBz, contacts);

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

  const v8ui keep = oldLocalAx != 0, reset = ~keep;
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
  const v8ui hasTangentLength = tangentLength > 0;
  const v8sf sfFb = mask(hasTangentLength,
                         staticFrictionDamping * (SDx * RVx + SDy * RVy + SDz * RVz));
  const v8ui hasStaticFriction = (tangentLength < staticFrictionLength)
                                              & (tangentRelativeSpeed < staticFrictionSpeed);
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
  scatter(grainMembraneLocalAx, contacts, localAx);
  scatter(grainMembraneLocalAy, contacts, localAy);
  scatter(grainMembraneLocalAz, contacts, localAz);
  scatter(grainMembraneLocalBx, contacts, localBx);
  scatter(grainMembraneLocalBy, contacts, localBy);
  scatter(grainMembraneLocalBz, contacts, localBz);
 }
}

void Simulation::stepGrainMembrane() {
 if(!grain.count || !membrane.count) return;
 if(grainMembraneGlobalMinD <= 0)  {

  vec3 min, max; domain(min, max);
  Grid grid(1/(Grain::radius+Grain::radius), min, max);
  for(size_t i: range(1, membrane.H)) {
   for(size_t j: range(membrane.W)) {
    size_t stride = membrane.stride;
    size_t k = i*stride+simd+j;
    grid.cell(membrane.Px[k], membrane.Py[k], membrane.Pz[k]).append(1+k);
   }
  }

  const float verletDistance = 2*(2*Grain::radius/sqrt(3.)) - (Grain::radius+0);
  //const float verletDistance = Grain::radius + Grain::radius;
  assert_(verletDistance > Grain::radius + 0);
  assert_(verletDistance <= Grain::radius + Grain::radius);
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  float minD = __builtin_inff();

  const int X = grid.size.x, Y = grid.size.y;
  const unused uint16* membraneNeighbours[3*3] = {
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

  swap(oldGrainMembraneA, grainMembraneA);
  swap(oldGrainMembraneB, grainMembraneB);
  swap(oldGrainMembraneLocalAx, grainMembraneLocalAx);
  swap(oldGrainMembraneLocalAy, grainMembraneLocalAy);
  swap(oldGrainMembraneLocalAz, grainMembraneLocalAz);
  swap(oldGrainMembraneLocalBx, grainMembraneLocalBx);
  swap(oldGrainMembraneLocalBy, grainMembraneLocalBy);
  swap(oldGrainMembraneLocalBz, grainMembraneLocalBz);

  static constexpr size_t averageGrainMembraneContactCount = 16;
  const size_t GWcc = align(simd, grain.count * averageGrainMembraneContactCount + 1);
  if(GWcc > grainMembraneA.capacity) {
   grainMembraneA = buffer<uint>(GWcc, 0);
   grainMembraneB = buffer<uint>(GWcc, 0);
   grainMembraneLocalAx = buffer<float>(GWcc, 0);
   grainMembraneLocalAy = buffer<float>(GWcc, 0);
   grainMembraneLocalAz = buffer<float>(GWcc, 0);
   grainMembraneLocalBx = buffer<float>(GWcc, 0);
   grainMembraneLocalBy = buffer<float>(GWcc, 0);
   grainMembraneLocalBz = buffer<float>(GWcc, 0);
  }
  grainMembraneA.size = 0;
  grainMembraneB.size = 0;
  grainMembraneLocalAx.size = 0;
  grainMembraneLocalAy.size = 0;
  grainMembraneLocalAz.size = 0;
  grainMembraneLocalBx.size = 0;
  grainMembraneLocalBy.size = 0;
  grainMembraneLocalBz.size = 0;

  size_t grainMembraneIndex = 0; // Index of first contact with A in old grainMembrane[Local]A|B list
  grainMembraneSearchTime += parallel_chunk(grain.count, [&](uint, size_t start, size_t size) {
   for(size_t a=start; a<(start+size); a+=1) { // TODO: SIMD
     size_t offset = grid.index(grain.Px[a], grain.Py[a], grain.Pz[a]);
     for(size_t n: range(3*3)) for(size_t i: range(3)) {
      ref<uint16> list(membraneNeighbours[n] + offset + i * Grid::cellCapacity, Grid::cellCapacity);

      for(size_t j: range(Grid::cellCapacity)) {
       assert_(list.begin() >= grid.cells.begin() && list.end()<=grid.cells.end(), offset, n, i);
       size_t b = list[j];
       if(!b) break;
       b--;
       float d = sqrt(sq(grain.Px[a]-membrane.Px[b])
                      + sq(grain.Py[a]-membrane.Py[b])
                      + sq(grain.Pz[a]-membrane.Pz[b])); // TODO: SIMD //FIXME: fails with Ofast?
       if(d > verletDistance) { minD=::min(minD, d); continue; }
       assert_(grainMembraneA.size < grainMembraneA.capacity);
       grainMembraneA.append( a ); // Grain
       grainMembraneB.append( b ); // Membrane
       for(size_t k = 0;; k++) {
        size_t j = grainMembraneIndex+k;
        if(j >= oldGrainMembraneA.size || oldGrainMembraneA[grainMembraneIndex+k] != a) break;
        if(oldGrainMembraneB[j] == b) { // Repack existing friction
         grainMembraneLocalAx.append( oldGrainMembraneLocalAx[j] );
         grainMembraneLocalAy.append( oldGrainMembraneLocalAy[j] );
         grainMembraneLocalAz.append( oldGrainMembraneLocalAz[j] );
         grainMembraneLocalBx.append( oldGrainMembraneLocalBx[j] );
         grainMembraneLocalBy.append( oldGrainMembraneLocalBy[j] );
         grainMembraneLocalBz.append( oldGrainMembraneLocalBz[j] );
         goto break_;
        }
       } /*else*/ { // New contact
        // Appends zero to reserve slot. Zero flags contacts for evaluation.
        // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
        grainMembraneLocalAx.append( 0 );
        grainMembraneLocalAy.append( 0 );
        grainMembraneLocalAz.append( 0 );
        grainMembraneLocalBx.append( 0 );
        grainMembraneLocalBy.append( 0 );
        grainMembraneLocalBz.append( 0 );
       }
break_:;
      }
      while(grainMembraneIndex < oldGrainMembraneA.size && oldGrainMembraneA[grainMembraneIndex] == a)
       grainMembraneIndex++;
     }
    }
  }, 1);

  assert_(align(simd, grainMembraneA.size+1) <= grainMembraneA.capacity);
  for(size_t i=grainMembraneA.size; i<align(simd, grainMembraneA.size+1); i++) grainMembraneA.begin()[i] = 0;
  assert_(align(simd, grainMembraneB.size+1) <= grainMembraneB.capacity);
  for(size_t i=grainMembraneB.size; i<align(simd, grainMembraneB.size+1); i++) grainMembraneB.begin()[i] = 0;

  grainMembraneGlobalMinD = minD - (Grain::radius+0);
  if(grainMembraneGlobalMinD < 0) log("grainMembraneGlobalMinD", grainMembraneGlobalMinD);

  /*if(processState > ProcessState::Pour) // Element creation resets verlet lists
   log("grain-membrane", grainMembraneSkipped);*/
  grainMembraneSkipped=0;
 } else grainMembraneSkipped++;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainMembraneA.size) > grainMembraneContact.capacity) {
  grainMembraneContact = buffer<uint>(align(simd, grainMembraneA.size));
 }
 grainMembraneContact.size = 0;
 grainMembraneFilterTime += parallel_chunk(align(simd, grainMembraneA.size)/simd, [&](uint, size_t start, size_t size) {
   for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    v8ui A = *(v8ui*)(grainMembraneA.data+i), B = *(v8ui*)(grainMembraneB.data+i);
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf Bx = gather(membrane.Px, B), By = gather(membrane.Py, B), Bz = gather(membrane.Pz, B);
    v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
    v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
    v8sf depth = float8(Grain::radius+0) - length;
    for(size_t k: range(simd)) {
     size_t j = i+k;
     if(j == grainMembraneA.size) break /*2*/;
     if(depth[k] > 0) {
      // Creates a map from packed contact to index into unpacked contact list (indirect reference)
      // Instead of packing (copying) the unpacked list to a packed contact list
      // To keep track of where to write back (unpacked) contact positions (for static friction)
      // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
      grainMembraneContact.append( j );
     } else {
      // Resets contact (static friction spring)
      grainMembraneLocalAx[j] = 0; /*grainMembraneLocalAy[j] = 0; grainMembraneLocalAz[j] = 0;
      grainMembraneLocalBx[j] = 0; grainMembraneLocalBy[j] = 0; grainMembraneLocalBz[j] = 0;*/
     }
    }
   }
 }, 1);
 for(size_t i=grainMembraneContact.size; i<align(simd, grainMembraneContact.size); i++)
  grainMembraneContact.begin()[i] = grainMembraneA.size;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, grainMembraneContact.size); // Grain-Membrane contact count
 if(GWcc > grainMembraneFx.capacity) {
  grainMembraneFx = buffer<float>(GWcc);
  grainMembraneFy = buffer<float>(GWcc);
  grainMembraneFz = buffer<float>(GWcc);
  grainMembraneTAx = buffer<float>(GWcc);
  grainMembraneTAy = buffer<float>(GWcc);
  grainMembraneTAz = buffer<float>(GWcc);
 }
 grainMembraneFx.size = GWcc;
 grainMembraneFy.size = GWcc;
 grainMembraneFz.size = GWcc;
 grainMembraneTAx.size = GWcc;
 grainMembraneTAy.size = GWcc;
 grainMembraneTAz.size = GWcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Grain::poissonRatio))/Grain::elasticModulus);
 constexpr float R = 1/(Grain::curvature+Membrane::curvature);
 const float K = 4./3*E*sqrt(R);
 const float mass = 1/(1/Grain::mass+1/membrane.mass);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainMembraneEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
    evaluateGrainMembrane(start, size,
                      grainMembraneContact.data, grainMembraneContact.size,
                      grainMembraneA.data, grainMembraneB.data,
                      grain.Px.data, grain.Py.data, grain.Pz.data,
                      membrane.Px.data, membrane.Py.data, membrane.Pz.data,
                      float8(Grain::radius+0), float8(Grain::radius), float8(0),
                      grainMembraneLocalAx.begin(), grainMembraneLocalAy.begin(), grainMembraneLocalAz.begin(),
                      grainMembraneLocalBx.begin(), grainMembraneLocalBy.begin(), grainMembraneLocalBz.begin(),
                      float8(K), float8(Kb),
                      float8(staticFrictionStiffness), float8(dynamicFrictionCoefficient),
                      float8(staticFrictionLength), float8(staticFrictionSpeed), float8(staticFrictionDamping),
                      grain.Vx.data, grain.Vy.data, grain.Vz.data,
                      membrane.Vx.data, membrane.Vy.data, membrane.Vz.data,
                      grain.AVx.data, grain.AVy.data, grain.AVz.data,
                      grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                      grainMembraneFx.begin(), grainMembraneFy.begin(), grainMembraneFz.begin(),
                      grainMembraneTAx.begin(), grainMembraneTAy.begin(), grainMembraneTAz.begin() );
 });
 grainMembraneContactSizeSum += grainMembraneContact.size;

 grainMembraneSumTime.start();
 for(size_t i = 0; i < grainMembraneContact.size; i++) { // Scalar scatter add
  size_t index = grainMembraneContact[i];
  size_t a = grainMembraneA[index];
  size_t b = grainMembraneB[index];
  grain.Fx[a] += grainMembraneFx[i];
  membrane .Fx[b] -= grainMembraneFx[i];
  grain.Fy[a] += grainMembraneFy[i];
  membrane .Fy[b] -= grainMembraneFy[i];
  grain.Fz[a] += grainMembraneFz[i];
  membrane .Fz[b] -= grainMembraneFz[i];
  grain.Tx[a] += grainMembraneTAx[i];
  grain.Ty[a] += grainMembraneTAy[i];
  grain.Tz[a] += grainMembraneTAz[i];
 }
 grainMembraneSumTime.stop();
}
