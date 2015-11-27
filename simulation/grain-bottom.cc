// TODO: Verlet
#include "simulation.h"

bool Simulation::stepGrainBottom() {
 {
  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageGrainBottomContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainBottomContactCount);
  buffer<uint> grainBottomA           (GBcc, 0);
  buffer<float> grainBottomLocalAx (GBcc, 0);
  buffer<float> grainBottomLocalAy (GBcc, 0);
  buffer<float> grainBottomLocalAz (GBcc, 0);
  buffer<float> grainBottomLocalBx (GBcc, 0);
  buffer<float> grainBottomLocalBy (GBcc, 0);
  buffer<float> grainBottomLocalBz (GBcc, 0);

  size_t grainBottomI = 0; // Index of first contact with A in old grainBottom[Local]A|B list
  for(size_t a: range(grain.count)) { // TODO: SIMD
   if(grain.Pz[a] > Grain::radius) continue;
   grainBottomA.append( a ); // Grain
   size_t j = grainBottomI;
   if(grainBottomI < this->grainBottomA.size && this->grainBottomA[grainBottomI] == a) {
    // Repack existing friction
    grainBottomLocalAx.append( this->grainBottomLocalAx[j] );
    grainBottomLocalAy.append( this->grainBottomLocalAy[j] );
    grainBottomLocalAz.append( this->grainBottomLocalAz[j] );
    grainBottomLocalBx.append( this->grainBottomLocalBx[j] );
    grainBottomLocalBy.append( this->grainBottomLocalBy[j] );
    grainBottomLocalBz.append( this->grainBottomLocalBz[j] );
   } else { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainBottomLocalAx.append( 0 );
    grainBottomLocalAy.append( 0 );
    grainBottomLocalAz.append( 0 );
    grainBottomLocalBx.append( 0 );
    grainBottomLocalBy.append( 0 );
    grainBottomLocalBz.append( 0 );
   }
   while(grainBottomI < this->grainBottomA.size && this->grainBottomA[grainBottomI] == a)
    grainBottomI++;
  }

  for(size_t i=grainBottomA.size; i<align(simd, grainBottomA.size); i++)
   grainBottomA.begin()[i] = 0;
  this->grainBottomA = move(grainBottomA);
  this->grainBottomLocalAx = move(grainBottomLocalAx);
  this->grainBottomLocalAy = move(grainBottomLocalAy);
  this->grainBottomLocalAz = move(grainBottomLocalAz);
  this->grainBottomLocalBx = move(grainBottomLocalBx);
  this->grainBottomLocalBy = move(grainBottomLocalBy);
  this->grainBottomLocalBz = move(grainBottomLocalBz);
 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainBottomA.size); // Grain-Wire contact count
 buffer<float> Fx(GBcc), Fy(GBcc), Fz(GBcc);
 buffer<float> TAx(GBcc), TAy(GBcc), TAz(GBcc);
 for(size_t index = 0; index < GBcc; index += 8) { // FIXME: parallel
  v8ui A = *(v8ui*)(grainBottomA.data+index);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf depth = float8(Grain::radius) - gather(grain.Pz, A);
  // Gather static frictions
  v8sf localAx = *(v8sf*)(grainBottomLocalAx.data+index);
  v8sf localAy = *(v8sf*)(grainBottomLocalAy.data+index);
  v8sf localAz = *(v8sf*)(grainBottomLocalAz.data+index);
  v8sf localBx = *(v8sf*)(grainBottomLocalBx.data+index);
  v8sf localBy = *(v8sf*)(grainBottomLocalBy.data+index);
  v8sf localBz = *(v8sf*)(grainBottomLocalBz.data+index);
  contact<Grain, Obstacle>(grain, A, depth,
                      _0f, _0f, float8(-Grain::radius),
                      _0f, _0f, _1f,
                      Ax, Ay, Az,
                      localAx, localAy, localAz,
                      localBx, localBy, localBz,
                      *(v8sf*)&Fx[index], *(v8sf*)&Fy[index], *(v8sf*)&Fz[index],
                      *(v8sf*)&TAx[index], *(v8sf*)&TAy[index], *(v8sf*)&TAz[index]
                      );
  assert_(isNumber(Fx[index]), index, grainBottomA);
  // Scatter static frictions
  *(v8sf*)(grainBottomLocalAx.data+index) = localAx;
  *(v8sf*)(grainBottomLocalAy.data+index) = localAy;
  *(v8sf*)(grainBottomLocalAz.data+index) = localAz;
  *(v8sf*)(grainBottomLocalBx.data+index) = localBx;
  *(v8sf*)(grainBottomLocalBy.data+index) = localBy;
  *(v8sf*)(grainBottomLocalBz.data+index) = localBz;
 }

 for(size_t i = 0; i < grainBottomA.size; i++) { // Scalar scatter add
  assert_(isNumber(Fx[i]), i, grainBottomA, GBcc);
  size_t a = grainBottomA[i];
  grain.Fx[a] += Fx[i];
  grain.Fy[a] += Fy[i];
  grain.Fz[a] += Fz[i];
  vec3 relativeA = qapply(grain.rotation[a],
                                    vec3(grainBottomLocalAx[i], grainBottomLocalAy[i], grainBottomLocalAz[i]));
  forces.append(grain.position(a) + relativeA, vec3(Fx[i], Fy[i], Fz[i]));

  grain.Tx[a] += TAx[i];
  grain.Ty[a] += TAy[i];
  grain.Tz[a] += TAz[i];
 }

 return true;
}
