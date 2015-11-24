#include "simulation.h"
#include "parallel.h"

bool Simulation::stepGrainGrain() {
 if(grainGrainGlobalMinD12 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
#if 0
  buffer<int2> grainGrainIndices {align(8, grain.count) * grainGrain};
  grainGrainIndices.clear();
  // Index to packed frictions
  for(size_t index: range(grainGrainCount)) {
   { // A<->B
    size_t a = grainGrainA[index];
    size_t base = a*grainGrain;
    size_t i = 0;
    while(grainGrainIndices[base+i]) i++;
    assert_(i<grainGrain, grainGrainIndices.slice(base, grainGrain));
    grainGrainIndices[base+i] = int2{grainGrainB[index]+1, int(index)};
   }
   /*{ // B<->A FIXME?
     size_t b = grainGrainB[index];
     size_t base = b*grainGrain;
     size_t i = 0;
     while(grainGrainIndices[base+i]) i++;
     assert(i<grainGrain, grainGrainIndices.slice(base, grainGrain));
     grainGrainIndices[base+i] = int2{grainGrainA[index]+1, int(index)};
    }*/
  }
#endif

  unique<Lattice<uint16>> grainLattice = generateLattice(grain, Grain::radius);
  if(!grainLattice) return false;
  float minD12= 2*(2*Grain::radius/sqrt(3.));
  const int Z = grainLattice->size.z, Y = grainLattice->size.y, X = grainLattice->size.x;

  int di[62];
  size_t i = 0;
  for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
   di[i++] = z*Y*X + y*X + x;
  }
  assert(i==62);

  //grainGrainCount = 0;
  size_t averageGrainGrainContactCount = 8;
  buffer<float> grainGrainLocalAx {grain.count * averageGrainGrainContactCount};
  buffer<float> grainGrainLocalAy {grain.count * averageGrainGrainContactCount};
  buffer<float> grainGrainLocalAz {grain.count * averageGrainGrainContactCount};
  buffer<float> grainGrainLocalBx {grain.count * averageGrainGrainContactCount};
  buffer<float> grainGrainLocalBy {grain.count * averageGrainGrainContactCount};
  buffer<float> grainGrainLocalBz {grain.count * averageGrainGrainContactCount};
  for(size_t latticeIndex: range(Z*Y*X)) { // FIXME: grain ordered traversal to map previous frictions
   const uint16* current = grainLattice->base + latticeIndex;
   size_t a = *current;
   if(!a) continue;
   a--;
   // Neighbours
   list<grainGrain/*12*/> D;
   for(size_t n: range(62)) {
    size_t b = current[di[n]];
    if(!b) continue;
    b--;
    float d = sqrt(sq(grain.Px[a]-grain.Px[b]) + sq(grain.Py[a]-grain.Py[b]) + sq(grain.Pz[a]-grain.Pz[b]));
    if(!D.insert(d, b)) minD12 = ::min(minD12, d);
   }
   //assert(minD12 > 0, minD12);
   for(size_t i: range(D.size)) {
    size_t index = grainGrainCount;
    grainGrainA[index] = a;
    size_t B = D.elements[i].value;
    grainGrainB[index] = B;
    for(size_t i: range(grainGrain/*12*/)) {
     size_t b = grainGrainIndices[a*grainGrain+i][0];
     if(!b) break;
     b--;
     if(b == B) { // Repack existing friction
      size_t j = grainGrainIndices[a*grainGrain+i][1];
      grainGrainLocalAx[index] = this->grainGrainLocalAx[j];
      grainGrainLocalAy[index] = this->grainGrainLocalAy[j];
      grainGrainLocalAz[index] = this->grainGrainLocalAz[j];
      grainGrainLocalBx[index] = this->grainGrainLocalBx[j];
      grainGrainLocalBy[index] = this->grainGrainLocalBy[j];
      grainGrainLocalBz[index] = this->grainGrainLocalBz[j];
      goto break_;
     }
    } /*else*/ {
     grainGrainLocalAx[index] = 0; grainGrainLocalAy[index] = 0; grainGrainLocalAz[index] = 0;
     grainGrainLocalBx[index] = 0; grainGrainLocalBy[index] = 0; grainGrainLocalBz[index] = 0;
    }
break_:;
    grainGrainCount++;
   }
  }

  //grainGrainCount2 = ::min(grainGrainCount2, grainGrainCount);
  this->grainGrainLocalAx = move(grainGrainLocalAx);
  this->grainGrainLocalAy = move(grainGrainLocalAy);
  this->grainGrainLocalAz = move(grainGrainLocalAz);
  this->grainGrainLocalBx = move(grainGrainLocalBx);
  this->grainGrainLocalBy = move(grainGrainLocalBy);
  this->grainGrainLocalBz = move(grainGrainLocalBz);
  //grainGrainCount2 = grainGrainCount;

  assert(grainGrainCount < grainCount8 * grainGrain);
  {// Aligns with invalid pairs
   size_t grainGrainCount = this->grainGrainCount;
   assert(grain.count < grain.capacity);
   // Need at least one invalid for packed
   grainGrainA[grainGrainCount] = grain.count;
   grainGrainB[grainGrainCount] = grain.count;
   grainGrainCount++;
   while(grainGrainCount%8) {
    grainGrainA[grainGrainCount] = grain.count;
    grainGrainB[grainGrainCount] = grain.count;
    grainGrainCount++;
   }
  }

  grainGrainGlobalMinD12 = minD12 - 2*Grain::radius;
  if(grainGrainGlobalMinD12 < 0) log("grainGrainGlobalMinD12", grainGrainGlobalMinD12);

  //log("grain", grainSkipped);
  grainSkipped=0;
 } else grainSkipped++;

 // Evaluates (packed) intersections from (packed) verlet lists
 size_t grainGrainContactCount = 0;
 buffer<uint> grainGrainContact(grain.count * averageGrainGrainContactCount);
 for(size_t index = 0; index < grainGrainCount; index += 8) {
  v8si A = *(v8si*)(grainGrainA.data+index), B = *(v8si*)(grainGrainB.data+index);
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf depth = float8(Grain::radius+Grain::radius) - length;
  for(size_t k: range(8)) {
   if(index+k < grainGrainCount && depth[k] > 0) {
    // Indirect instead of pack to write back friction points
    grainGrainContact[grainGrainContactCount] = index+k;
    grainGrainContactCount++;
   } else {
    grainGrainLocalAx[index+k] = 0; grainGrainLocalAy[index+k] = 0; grainGrainLocalAz[index+k] = 0;
    grainGrainLocalBx[index+k] = 0; grainGrainLocalBy[index+k] = 0; grainGrainLocalBz[index+k] = 0;
   }
  }
 }

 // Aligns with invalid contacts
 size_t gGCC = grainGrainContactCount;
 while(gGCC%8) grainGrainContact[gGCC++] = grainGrainCount /*invalid*/;

 // Evaluates forces from (packed) intersections
 buffer<float> Fx(gGCC), Fy(gGCC), Fz(gGCC);
 buffer<float> TAx(gGCC), TAy(gGCC), TAz(gGCC);
 buffer<float> TBx(gGCC), TBy(gGCC), TBz(gGCC);

 parallel_chunk(align(8, grainGrainContactCount), [&](uint, size_t start, size_t size) {
  for(size_t i=start*8; i < start*8+size*8; i+=8) {
   v8si contacts = *(v8si*)&grainGrainContact[i];
   v8si A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
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

 for(size_t i = 0; i < grainGrainContactCount; i++) { // Scalar scatter add
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
