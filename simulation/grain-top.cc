// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

void Simulation::stepGrainTop() {
 {
  swap(oldGrainTopA, grainTopA);
  swap(oldGrainTopLocalAx, grainTopLocalAx);
  swap(oldGrainTopLocalAy, grainTopLocalAy);
  swap(oldGrainTopLocalAz, grainTopLocalAz);
  swap(oldGrainTopLocalBx, grainTopLocalBx);
  swap(oldGrainTopLocalBy, grainTopLocalBy);
  swap(oldGrainTopLocalBz, grainTopLocalBz);

  static constexpr size_t averageGrainTopContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainTopContactCount);
  if(GBcc > grainTopA.capacity) {
   grainTopA = buffer<uint>(GBcc, 0);
   grainTopLocalAx = buffer<float>(GBcc, 0);
   grainTopLocalAy = buffer<float>(GBcc, 0);
   grainTopLocalAz = buffer<float>(GBcc, 0);
   grainTopLocalBx = buffer<float>(GBcc, 0);
   grainTopLocalBy = buffer<float>(GBcc, 0);
   grainTopLocalBz = buffer<float>(GBcc, 0);
  }
  grainTopA.size = 0;
  grainTopLocalAx.size = 0;
  grainTopLocalAy.size = 0;
  grainTopLocalAz.size = 0;
  grainTopLocalBx.size = 0;
  grainTopLocalBy.size = 0;
  grainTopLocalBz.size = 0;

  size_t grainTopI = 0; // Index of first contact with A in old grainTop[Local]A|B list
  grainTopFilterTime += parallel_chunk(grain.count, [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
     if(grain.Pz[i] < topZ-Grain::radius) continue;
     grainTopA.append( i ); // Grain
     size_t j = grainTopI;
     if(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i) {
      // Repack existing friction
      grainTopLocalAx.append( oldGrainTopLocalAx[j] );
      grainTopLocalAy.append( oldGrainTopLocalAy[j] );
      grainTopLocalAz.append( oldGrainTopLocalAz[j] );
      grainTopLocalBx.append( oldGrainTopLocalBx[j] );
      grainTopLocalBy.append( oldGrainTopLocalBy[j] );
      grainTopLocalBz.append( oldGrainTopLocalBz[j] );
     } else { // New contact
      // Appends zero to reserve slot. Zero flags contacts for evaluation.
      // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
      grainTopLocalAx.append( 0 );
      grainTopLocalAy.append( 0 );
      grainTopLocalAz.append( 0 );
      grainTopLocalBx.append( 0 );
      grainTopLocalBy.append( 0 );
      grainTopLocalBz.append( 0 );
     }
     while(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i)
      grainTopI++;
    }
  }, 1);

  for(size_t i=grainTopA.size; i<align(simd, grainTopA.size); i++)
   grainTopA.begin()[i] = 0;
 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainTopA.size); // Grain-Wire contact count
 if(GBcc > grainTopFx.capacity) {
  grainTopFx = buffer<float>(GBcc);
  grainTopFy = buffer<float>(GBcc);
  grainTopFz = buffer<float>(GBcc);
  grainTopTAx = buffer<float>(GBcc);
  grainTopTAy = buffer<float>(GBcc);
  grainTopTAz = buffer<float>(GBcc);
 }
 grainTopFx.size = GBcc;
 grainTopFy.size = GBcc;
 grainTopFz.size = GBcc;
 grainTopTAx.size = GBcc;
 grainTopTAy.size = GBcc;
 grainTopTAz.size = GBcc;
 grainTopEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    v8ui A = *(v8ui*)(grainTopA.data+i);
    // FIXME: Recomputing from intersection (more efficient than storing?)
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf depth = gather(grain.Pz, A) - float8(topZ-Grain::radius);
    // Gather static frictions
    v8sf localAx = *(v8sf*)(grainTopLocalAx.data+i);
    v8sf localAy = *(v8sf*)(grainTopLocalAy.data+i);
    v8sf localAz = *(v8sf*)(grainTopLocalAz.data+i);
    v8sf localBx = *(v8sf*)(grainTopLocalBx.data+i);
    v8sf localBy = *(v8sf*)(grainTopLocalBy.data+i);
    v8sf localBz = *(v8sf*)(grainTopLocalBz.data+i);
    // FIXME: inline full SIMD
    contact<Grain, Obstacle>(grain, A, depth,
                             _0f, _0f, float8(Grain::radius),
                             _0f, _0f, -_1f,
                             Ax, Ay, Az,
                             localAx, localAy, localAz,
                             localBx, localBy, localBz,
                             *(v8sf*)&grainTopFx[i], *(v8sf*)&grainTopFy[i], *(v8sf*)&grainTopFz[i],
                             *(v8sf*)&grainTopTAx[i], *(v8sf*)&grainTopTAy[i], *(v8sf*)&grainTopTAz[i]
                             );
    // Store static frictions
    *(v8sf*)(grainTopLocalAx.data+i) = localAx;
    *(v8sf*)(grainTopLocalAy.data+i) = localAy;
    *(v8sf*)(grainTopLocalAz.data+i) = localAz;
    *(v8sf*)(grainTopLocalBx.data+i) = localBx;
    *(v8sf*)(grainTopLocalBy.data+i) = localBy;
    *(v8sf*)(grainTopLocalBz.data+i) = localBz;
   }
 }, 1);

 grainTopSumTime.start();
 for(size_t i=0; i<grainTopA.size; i++) { // Scalar scatter add
  size_t a = grainTopA[i];
  grain.Fx[a] += grainTopFx[i];
  grain.Fy[a] += grainTopFy[i];
  grain.Fz[a] += grainTopFz[i];
  grain.Tx[a] += grainTopTAx[i];
  grain.Ty[a] += grainTopTAy[i];
  grain.Tz[a] += grainTopTAz[i];
 }
 grainTopSumTime.stop();
}
