// TODO: Cylinder contacts
#include "simulation.h"
#include "parallel.h"

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
 grainWireEvaluateTime += parallel_chunk(GWcc/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) { // Preserves alignment
   v8ui contacts = *(v8ui*)(grainWireContact.data+i);
   v8ui A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
   // FIXME: Recomputing from intersection (more efficient than storing?)
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(wire.Px, B), By = gather(wire.Py, B), Bz = gather(wire.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = float8(Grain::radius+Wire::radius) - length;
   v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
   sconst v8sf R = float8(Grain::radius);
   v8sf RAx = - R  * Nx, RAy = - R * Ny, RAz = - R * Nz;
   // Gather static frictions
   v8sf localAx = gather(grainWireLocalAx, contacts);
   v8sf localAy = gather(grainWireLocalAy, contacts);
   v8sf localAz = gather(grainWireLocalAz, contacts);
   v8sf localBx = gather(grainWireLocalBx, contacts);
   v8sf localBy = gather(grainWireLocalBy, contacts);
   v8sf localBz = gather(grainWireLocalBz, contacts);
   contact<Grain,Wire>(grain, A, wire, B, depth,
                       RAx, RAy, RAz,
                       Nx, Ny, Nz,
                       Ax, Ay, Az,
                       Bx, By, Bz,
                       localAx, localAy, localAz,
                       localBx, localBy, localBz,
                       *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                       *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i]
                       );
   // Scatter static frictions
   scatter(grainWireLocalAx, contacts, localAx);
   scatter(grainWireLocalAy, contacts, localAy);
   scatter(grainWireLocalAz, contacts, localAz);
   scatter(grainWireLocalBx, contacts, localBx);
   scatter(grainWireLocalBy, contacts, localBy);
   scatter(grainWireLocalBz, contacts, localBz);
  }
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
