#include "simulation.h"
#include "parallel.h"

bool Simulation::stepGrainGrain() {
 if(grainGrainGlobalMinD12 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
  unique<Lattice<uint16>> grainLattice = generateLattice(grain, Grain::radius);
  if(!grainLattice) return false;
  float minD12= 2*(2*Grain::radius/sqrt(3.));

  const int Y = grainLattice->size.y, X = grainLattice->size.x;
  const uint16* latticeNeighbours[62];
  size_t i = 0;
  for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
   latticeNeighbours[i++] = grainLattice->base + z*Y*X + y*X + x;
  }
  assert(i==62);

  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageGrainGrainContactCount = 8;
  buffer<uint> grainGrainA (grain.count * averageGrainGrainContactCount, 0);
  buffer<uint> grainGrainB (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalAx (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalAy (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalAz (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalBx (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalBy (grain.count * averageGrainGrainContactCount, 0);
  buffer<float> grainGrainLocalBz (grain.count * averageGrainGrainContactCount, 0);
  size_t grainGrainIndex = 0; // Index of first contact with A in old grainGrain[Local]A|B list
  for(size_t a: range(grain.count)) {
   size_t offset = grainLattice->index(grain.Px[a], grain.Py[a], grain.Pz[a]);

   // Neighbours
   static constexpr size_t maximumGrainGrainContactCount = 12;
   list<maximumGrainGrainContactCount> D;
   for(size_t n: range(62)) {
    size_t b = *(latticeNeighbours[n] + offset);
    if(!b) continue;
    b--;
    float d = sqrt(sq(grain.Px[a]-grain.Px[b])
                      + sq(grain.Py[a]-grain.Py[b])
                      + sq(grain.Pz[a]-grain.Pz[b]));
    if(!D.insert(d, b)) minD12 = ::min(minD12, d);
   }
   for(size_t i: range(D.size)) {
    size_t b = D.elements[i].value;
    grainGrainA.append( a );
    grainGrainB.append( b );
    for(size_t k = 0; this->grainGrainA[grainGrainIndex+k] == a; k++) {
     size_t j = grainGrainIndex+k;
     if(grainGrainB[j] == b) { // Repack existing friction
      grainGrainLocalAx.append( this->grainGrainLocalAx[j] );
      grainGrainLocalAy.append( this->grainGrainLocalAy[j] );
      grainGrainLocalAz.append( this->grainGrainLocalAz[j] );
      grainGrainLocalBx.append( this->grainGrainLocalBx[j] );
      grainGrainLocalBy.append( this->grainGrainLocalBy[j] );
      grainGrainLocalBz.append( this->grainGrainLocalBz[j] );
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
   while(grainGrainIndex < this->grainGrainA.size-1 && this->grainGrainA[grainGrainIndex] == a)
    grainGrainIndex++;
  }

  this->grainGrainA = move(grainGrainA);
  this->grainGrainB = move(grainGrainB);
  this->grainGrainLocalAx = move(grainGrainLocalAx);
  this->grainGrainLocalAy = move(grainGrainLocalAy);
  this->grainGrainLocalAz = move(grainGrainLocalAz);
  this->grainGrainLocalBx = move(grainGrainLocalBx);
  this->grainGrainLocalBy = move(grainGrainLocalBy);
  this->grainGrainLocalBz = move(grainGrainLocalBz);

  grainGrainGlobalMinD12 = minD12 - 2*Grain::radius;
  if(grainGrainGlobalMinD12 < 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD12);

  //log("grain", grainSkipped);
  grainSkipped=0;
 } else grainSkipped++;

 // Evaluates (packed) intersections from (packed) verlet lists
 buffer<uint> grainGrainContact(grainGrainA.size);
 for(size_t index = 0; index < grainGrainA.size; index += 8) {
  v8ui A = *(v8ui*)(grainGrainA.data+index), B = *(v8ui*)(grainGrainB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Grain::radius) - length;
  for(size_t k: range(8)) {
   size_t j = index+k;
   if(j == grainGrainA.size) break /*2*/;
   if(depth[k] > 0) {
    // Creates a map from packed contact to index into unpacked contact list (indirect reference)
    // Instead of packing (copying) the unpacked list to a packed contact list
    // To keep track of where to write back (unpacked) contact positions (for static friction)
    // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
    grainGrainContact.append( j );
   } else {
    // Resets contact (static friction spring)
    grainGrainLocalAx[j] = 0; grainGrainLocalAy[j] = 0; grainGrainLocalAz[j] = 0;
    grainGrainLocalBx[j] = 0; grainGrainLocalBy[j] = 0; grainGrainLocalBz[j] = 0;
   }
  }
 }

 // Evaluates forces from (packed) intersections (SoA)
 size_t GGcc = grainGrainA.size; // Grain-Grain contact count
 buffer<float> Fx(GGcc), Fy(GGcc), Fz(GGcc);
 buffer<float> TAx(GGcc), TAy(GGcc), TAz(GGcc);
 buffer<float> TBx(GGcc), TBy(GGcc), TBz(GGcc);

 parallel_chunk(align(8, GGcc), [&](uint, size_t start, size_t size) {
  for(size_t i=start*8; i < start*8+size*8; i+=8) {
   v8ui contacts = *(v8ui*)&grainGrainContact[i];
   v8ui A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
   // FIXME: Recomputing from intersection (more efficient than storing?)
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = float8(Grain::radius+Grain::radius) - length;
   v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
   sconst v8sf R = float8(Grain::radius);
   v8sf RBx = R * Nx, RBy = R * Ny, RBz = R * Nz;
   v8sf RAx = -RBx, RAy = -RBy, RAz = -RBz;
   // Gather static frictions
   v8sf localAx = gather(grainGrainLocalAx, contacts);
   v8sf localAy = gather(grainGrainLocalAy, contacts);
   v8sf localAz = gather(grainGrainLocalAz, contacts);
   v8sf localBx = gather(grainGrainLocalBx, contacts);
   v8sf localBy = gather(grainGrainLocalBy, contacts);
   v8sf localBz = gather(grainGrainLocalBz, contacts);
   contact<Grain, Grain>( grain, A, grain, B, depth,
                          RAx, RAy, RAz,
                          RBx, RBy, RBz,
                          Nx, Ny, Nz,
                          Ax, Ay, Az,
                          Bx, By, Bz,
                          localAx, localAy, localAz,
                          localBx, localBy, localBz,
                          *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                          *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i],
                          *(v8sf*)&TBx[i], *(v8sf*)&TBy[i], *(v8sf*)&TBz[i]
                          );
   // Scatter static frictions
   scatter(grainGrainLocalAx, contacts, localAx);
   scatter(grainGrainLocalAy, contacts, localAy);
   scatter(grainGrainLocalAz, contacts, localAz);
   scatter(grainGrainLocalBx, contacts, localBx);
   scatter(grainGrainLocalBy, contacts, localBy);
   scatter(grainGrainLocalBz, contacts, localBz);
  }});

 for(size_t i = 0; i < GGcc; i++) { // Scalar scatter add
  size_t index = grainGrainContact[i];
  size_t a = grainGrainA[index];
  size_t b = grainGrainB[index];
  grain.Fx[a] += Fx[i];
  grain.Fx[b] -= Fx[i];
  grain.Fy[a] += Fy[i];
  grain.Fy[b] -= Fy[i];
  grain.Fz[a] += Fz[i];
  grain.Fz[b] -= Fz[i];

  grain.Tx[a] += TAx[i];
  grain.Ty[a] += TAy[i];
  grain.Tz[a] += TAz[i];
  grain.Tx[b] += TBx[i];
  grain.Ty[b] += TBy[i];
  grain.Tz[b] += TBz[i];
 }

 return true;
}
