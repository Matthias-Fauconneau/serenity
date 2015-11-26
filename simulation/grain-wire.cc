// TODO: Verlet, Cylinder contacts
#include "simulation.h"

bool Simulation::stepGrainWire() {
 if(!wire.count) return true;
 {
  unique<Grid> wireGrid = generateGrid(wire, Grain::radius+Wire::radius);
  if(!wireGrid) return false;

  const int X = wireGrid->size.x, Y = wireGrid->size.y;
  const uint16* wireNeighbours[3*3] = {
   wireGrid->base+(-X*Y-X-1)*Grid::cellCapacity,
   wireGrid->base+(-X*Y-1)*Grid::cellCapacity,
   wireGrid->base+(-X*Y+X-1)*Grid::cellCapacity,

   wireGrid->base+(-X-1)*Grid::cellCapacity,
   wireGrid->base+(-1)*Grid::cellCapacity,
   wireGrid->base+(X-1)*Grid::cellCapacity,

   wireGrid->base+(X*Y-X-1)*Grid::cellCapacity,
   wireGrid->base+(X*Y-1)*Grid::cellCapacity,
   wireGrid->base+(X*Y+X-1)*Grid::cellCapacity
  };

  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageGrainWireContactCount = 8;
  const size_t GWcc = align(simd, grain.count * averageGrainWireContactCount);
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
   size_t offset = wireGrid->index(grain.Px[a], grain.Py[a], grain.Pz[a]);

   // Neighbours
   for(size_t n: range(3*3)) for(size_t i: range(3)) {
    ref<uint16> list(wireNeighbours[n] + offset + i * Grid::cellCapacity, Grid::cellCapacity);

    for(size_t j=0; list[j] && j<Grid::cellCapacity; j++) {
     size_t b = list[j]-1;
     grainWireA.append( a ); // Grain
     grainWireB.append( b ); // Wire
     for(size_t k = 0;; k++) {
      size_t j = grainWireIndex+k;
      if(j >= this->grainWireA.size || this->grainWireA[grainWireIndex+k] != a) break;
      if(grainWireB[j] == b) { // Repack existing friction
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
   }
   while(grainWireIndex < this->grainWireA.size && this->grainWireA[grainWireIndex] == a)
    grainWireIndex++;
  }

  for(size_t i=grainWireA.size; i<align(simd, grainWireA.size); i++) grainWireA.begin()[i] = 0;
  this->grainWireA = move(grainWireA);
  this->grainWireB = move(grainWireB);
  this->grainWireLocalAx = move(grainWireLocalAx);
  this->grainWireLocalAy = move(grainWireLocalAy);
  this->grainWireLocalAz = move(grainWireLocalAz);
  this->grainWireLocalBx = move(grainWireLocalBx);
  this->grainWireLocalBy = move(grainWireLocalBy);
  this->grainWireLocalBz = move(grainWireLocalBz);
 }

 // Evaluates (packed) intersections from (packed) (TODO: verlet) lists
 buffer<uint> grainWireContact(grainWireA.size);
 for(size_t index = 0; index < grainWireA.size; index += 8) {
  v8ui A = *(v8ui*)(grainWireA.data+index), B = *(v8ui*)(grainWireB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Wire::radius) - length;
  for(size_t k: range(8)) {
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
    grainWireLocalAx[index+k] = 0; grainWireLocalAy[index+k] = 0; grainWireLocalAz[index+k] = 0;
    grainWireLocalBx[index+k] = 0; grainWireLocalBy[index+k] = 0; grainWireLocalBz[index+k] = 0;
   }
  }
 }

 // Evaluates forces from (packed) intersections (SoA)
 size_t GWcc = align(simd, grainWireA.size); // Grain-Wire contact count
 buffer<float> Fx(GWcc), Fy(GWcc), Fz(GWcc);
 buffer<float> TAx(GWcc), TAy(GWcc), TAz(GWcc);
 for(size_t i = 0; i < GWcc; i += 8) { // FIXME: parallel
  v8ui contacts = *(v8ui*)(grainWireContact.data+i);
  v8ui A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
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

 for(size_t i = 0; i < GWcc; i++) { // Scalar scatter add
  assert_(isNumber(Fx[i]));
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

 return true;
}
