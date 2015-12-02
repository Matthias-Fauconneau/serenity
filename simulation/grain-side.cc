// TODO: Verlet, reintegrate soft membrane, face contacts
#include "simulation.h"

void Simulation::stepGrainSide() {
 {
  swap(oldGrainSideA, grainSideA);
  swap(oldGrainSideLocalAx, grainSideLocalAx);
  swap(oldGrainSideLocalAy, grainSideLocalAy);
  swap(oldGrainSideLocalAz, grainSideLocalAz);
  swap(oldGrainSideLocalBx, grainSideLocalBx);
  swap(oldGrainSideLocalBy, grainSideLocalBy);
  swap(oldGrainSideLocalBz, grainSideLocalBz);

  static constexpr size_t averageGrainSideContactCount = 1;
  const size_t GScc = align(simd, grain.count * averageGrainSideContactCount);
  if(GScc > grainSideA.capacity) {
   grainSideA = buffer<uint>(GScc, 0);
   grainSideLocalAx = buffer<float>(GScc, 0);
   grainSideLocalAy = buffer<float>(GScc, 0);
   grainSideLocalAz = buffer<float>(GScc, 0);
   grainSideLocalBx = buffer<float>(GScc, 0);
   grainSideLocalBy = buffer<float>(GScc, 0);
   grainSideLocalBz = buffer<float>(GScc, 0);
  }
  grainSideA.size = 0;
  grainSideLocalAx.size = 0;
  grainSideLocalAy.size = 0;
  grainSideLocalAz.size = 0;
  grainSideLocalBx.size = 0;
  grainSideLocalBy.size = 0;
  grainSideLocalBz.size = 0;

  size_t grainSideI = 0; // Index of first contact with A in old grainSide[Local]A|B list
  grainSideFilterTime.start();
  for(size_t a: range(grain.count)) { // TODO: SIMD
   if(sq(grain.Px[a]) + sq(grain.Py[a]) < sq(radius - Grain::radius)) continue;
   grainSideA.append( a ); // Grain
   size_t j = grainSideI;
   if(grainSideI < oldGrainSideA.size && oldGrainSideA[grainSideI] == a) {
    // Repack existing friction
    grainSideLocalAx.append( oldGrainSideLocalAx[j] );
    grainSideLocalAy.append( oldGrainSideLocalAy[j] );
    grainSideLocalAz.append( oldGrainSideLocalAz[j] );
    grainSideLocalBx.append( oldGrainSideLocalBx[j] );
    grainSideLocalBy.append( oldGrainSideLocalBy[j] );
    grainSideLocalBz.append( oldGrainSideLocalBz[j] );
   } else { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainSideLocalAx.append( 0 );
    grainSideLocalAy.append( 0 );
    grainSideLocalAz.append( 0 );
    grainSideLocalBx.append( 0 );
    grainSideLocalBy.append( 0 );
    grainSideLocalBz.append( 0 );
   }
   while(grainSideI < oldGrainSideA.size && oldGrainSideA[grainSideI] == a)
    grainSideI++;
  }
  grainSideFilterTime.stop();

  for(size_t i=grainSideA.size; i<align(simd, grainSideA.size); i++) grainSideA.begin()[i] = 0;

 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)
 size_t GScc = align(simd, grainSideA.size); // Grain-Grain contact count
 buffer<float> Fx(GScc), Fy(GScc), Fz(GScc);
 buffer<float> TAx(GScc), TAy(GScc), TAz(GScc);
 grainSideEvaluateTime.start();
 for(size_t index = 0; index < GScc; index += 8) { // FIXME: parallel
  v8ui A = *(v8ui*)(grainSideA.data+index);
  // FIXME: Recomputing from intersection (more efficient than storing?)
  v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
  v8sf length = sqrt(Ax*Ax + Ay*Ay);
  v8sf Nx = -Ax/length, Ny = -Ay/length;
  v8sf depth = length - float8(radius-Grain::radius);
  // Gather static frictions
  v8sf localAx = *(v8sf*)(grainSideLocalAx.data+index);
  v8sf localAy = *(v8sf*)(grainSideLocalAy.data+index);
  v8sf localAz = *(v8sf*)(grainSideLocalAz.data+index);
  v8sf localBx = *(v8sf*)(grainSideLocalBx.data+index);
  v8sf localBy = *(v8sf*)(grainSideLocalBy.data+index);
  v8sf localBz = *(v8sf*)(grainSideLocalBz.data+index);
  sconst v8sf gR = float8(Grain::radius);
  contact<Grain, Obstacle>(grain, A, depth,
                           -gR*Nx, -gR*Ny, _0f,
                           Nx, Ny, _0f,
                           Ax, Ay, Az,
                           localAx, localAy, localAz,
                           localBx, localBy, localBz,
                           *(v8sf*)&Fx[index], *(v8sf*)&Fy[index], *(v8sf*)&Fz[index],
                           *(v8sf*)&TAx[index], *(v8sf*)&TAy[index], *(v8sf*)&TAz[index]);
  // Scatter static frictions
  *(v8sf*)(grainSideLocalAx.data+index) = localAx;
  *(v8sf*)(grainSideLocalAy.data+index) = localAy;
  *(v8sf*)(grainSideLocalAz.data+index) = localAz;
  *(v8sf*)(grainSideLocalBx.data+index) = localBx;
  *(v8sf*)(grainSideLocalBy.data+index) = localBy;
  *(v8sf*)(grainSideLocalBz.data+index) = localBz;
 }
 grainSideEvaluateTime.stop();

 grainSideSumTime.start();
 for(size_t index = 0; index < grainSideA.size; index++) { // Scalar scatter add
  size_t a = grainSideA[index];
  grain.Fx[a] += Fx[index];
  grain.Fy[a] += Fy[index];
  grain.Fz[a] += Fz[index];

  grain.Tx[a] += TAx[index];
  grain.Ty[a] += TAy[index];
  grain.Tz[a] += TAz[index];
 }
 grainSideSumTime.stop();
}
