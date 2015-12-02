#include "simulation.h"
#include "parallel.h"

void Simulation::stepGrainGrain() {
 if(!grain.count) return;
 if(grainGrainGlobalMinD <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
  vec3 min, max; domain(min, max);
  Lattice<uint16> lattice(sqrt(3.)/(2*Grain::radius), min, max);
  for(size_t i: range(grain.count)) {
   lattice.cell(grain.Px[i], grain.Py[i], grain.Pz[i]) = 1+i;
  }

  const float verletDistance = 2*(2*Grain::radius/sqrt(3.)); // > Grain::radius + Grain::radius
  // Minimum distance over verlet distance parameter is the actual verlet distance which can be used
  float minD = inf;

  const int Y = lattice.size.y, X = lattice.size.x;
  const uint16* latticeNeighbours[62];
  size_t i = 0;
  for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
   latticeNeighbours[i++] = lattice.base.data + z*Y*X + y*X + x;
  }
  assert(i==62);

  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageGrainGrainContactCount = 7;
  size_t GGcc = align(simd, grain.count * averageGrainGrainContactCount);
  buffer<uint> grainGrainA (GGcc, 0);
  buffer<uint> grainGrainB (GGcc, 0);
  buffer<float> grainGrainLocalAx (GGcc, 0);
  buffer<float> grainGrainLocalAy (GGcc, 0);
  buffer<float> grainGrainLocalAz (GGcc, 0);
  buffer<float> grainGrainLocalBx (GGcc, 0);
  buffer<float> grainGrainLocalBy (GGcc, 0);
  buffer<float> grainGrainLocalBz (GGcc, 0);
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
     if(j >= this->grainGrainA.size || this->grainGrainA[j] != a) break;
     if(this->grainGrainB[j] == b) { // Repack existing friction
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
   while(grainGrainI < this->grainGrainA.size && this->grainGrainA[grainGrainI] == a)
    grainGrainI++;
  }

  assert_(align(simd, grainGrainA.size+1) <= grainGrainA.capacity);
  for(size_t i=grainGrainA.size; i<align(simd, grainGrainA.size+1); i++) grainGrainA.begin()[i] = 0;
  this->grainGrainA = move(grainGrainA);
  for(size_t i=grainGrainB.size; i<align(simd, grainGrainB.size+1); i++) grainGrainB.begin()[i] = 0;
  this->grainGrainB = move(grainGrainB);
  this->grainGrainLocalAx = move(grainGrainLocalAx);
  this->grainGrainLocalAy = move(grainGrainLocalAy);
  this->grainGrainLocalAz = move(grainGrainLocalAz);
  this->grainGrainLocalBx = move(grainGrainLocalBx);
  this->grainGrainLocalBy = move(grainGrainLocalBy);
  this->grainGrainLocalBz = move(grainGrainLocalBz);

  grainGrainGlobalMinD = minD - 2*Grain::radius;
  if(grainGrainGlobalMinD < 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD);

  //log("grain-grain", grainGrainSkipped);
  grainGrainSkipped=0;
 } else grainGrainSkipped++;

 // Evaluates (packed) intersections from (packed) verlet lists
 buffer<uint> grainGrainContact(align(simd, grainGrainA.size), 0);
 for(size_t index = 0; index < grainGrainA.size; index += 8) {
  v8ui A = *(v8ui*)(grainGrainA.data+index), B = *(v8ui*)(grainGrainB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
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
 for(size_t i=grainGrainContact.size; i<align(simd, grainGrainContact.size); i++)
  grainGrainContact.begin()[i] = 0;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GGcc = align(simd, grainGrainContact.size); // Grain-Grain contact count
 buffer<float> Fx(GGcc), Fy(GGcc), Fz(GGcc);
 buffer<float> TAx(GGcc), TAy(GGcc), TAz(GGcc);
 buffer<float> TBx(GGcc), TBy(GGcc), TBz(GGcc);

 //parallel_chunk(align(8, GGcc), [&](uint, size_t start, size_t size) {
  for(size_t index = 0; index < GGcc; index += 8) {
  //for(size_t index=start*8; index < start*8+size*8; index+=8) {
   v8ui contacts = *(v8ui*)&grainGrainContact[index];
   v8ui A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
   // FIXME: Recomputing from intersection (more efficient than storing?)
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
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
                          *(v8sf*)&Fx[index], *(v8sf*)&Fy[index], *(v8sf*)&Fz[index],
                          *(v8sf*)&TAx[index], *(v8sf*)&TAy[index], *(v8sf*)&TAz[index],
                          *(v8sf*)&TBx[index], *(v8sf*)&TBy[index], *(v8sf*)&TBz[index]
                          );
   // Scatter static frictions
   scatter(grainGrainLocalAx, contacts, localAx);
   scatter(grainGrainLocalAy, contacts, localAy);
   scatter(grainGrainLocalAz, contacts, localAz);
   scatter(grainGrainLocalBx, contacts, localBx);
   scatter(grainGrainLocalBy, contacts, localBy);
   scatter(grainGrainLocalBz, contacts, localBz);
  }//});

 for(size_t i = 0; i < grainGrainContact.size; i++) { // Scalar scatter add
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
}
