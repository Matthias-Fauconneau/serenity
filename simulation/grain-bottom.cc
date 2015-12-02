// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

void Simulation::stepGrainBottom() {
 {
  swap(oldGrainBottomA, grainBottomA);
  swap(oldGrainBottomLocalAx, grainBottomLocalAx);
  swap(oldGrainBottomLocalAy, grainBottomLocalAy);
  swap(oldGrainBottomLocalAz, grainBottomLocalAz);
  swap(oldGrainBottomLocalBx, grainBottomLocalBx);
  swap(oldGrainBottomLocalBy, grainBottomLocalBy);
  swap(oldGrainBottomLocalBz, grainBottomLocalBz);

  static constexpr size_t averageGrainBottomContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainBottomContactCount);
  if(GBcc > grainBottomA.capacity) {
   grainBottomA = buffer<uint>(GBcc, 0);
   grainBottomLocalAx = buffer<float>(GBcc, 0);
   grainBottomLocalAy = buffer<float>(GBcc, 0);
   grainBottomLocalAz = buffer<float>(GBcc, 0);
   grainBottomLocalBx = buffer<float>(GBcc, 0);
   grainBottomLocalBy = buffer<float>(GBcc, 0);
   grainBottomLocalBz = buffer<float>(GBcc, 0);
  }
  grainBottomA.size = 0;
  grainBottomLocalAx.size = 0;
  grainBottomLocalAy.size = 0;
  grainBottomLocalAz.size = 0;
  grainBottomLocalBx.size = 0;
  grainBottomLocalBy.size = 0;
  grainBottomLocalBz.size = 0;

  size_t grainBottomI = 0; // Index of first contact with A in old grainBottom[Local]A|B list
  grainBottomFilterTime += parallel_chunk(grain.count, [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
     if(grain.Pz[i] > Grain::radius) continue;
     grainBottomA.append( i ); // Grain
     size_t j = grainBottomI;
     if(grainBottomI < oldGrainBottomA.size && oldGrainBottomA[grainBottomI] == i) {
      // Repack existing friction
      grainBottomLocalAx.append( oldGrainBottomLocalAx[j] );
      grainBottomLocalAy.append( oldGrainBottomLocalAy[j] );
      grainBottomLocalAz.append( oldGrainBottomLocalAz[j] );
      grainBottomLocalBx.append( oldGrainBottomLocalBx[j] );
      grainBottomLocalBy.append( oldGrainBottomLocalBy[j] );
      grainBottomLocalBz.append( oldGrainBottomLocalBz[j] );
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
     while(grainBottomI < oldGrainBottomA.size && oldGrainBottomA[grainBottomI] == i)
      grainBottomI++;
    }
  }, 1);

  for(size_t i=grainBottomA.size; i<align(simd, grainBottomA.size); i++)
   grainBottomA.begin()[i] = 0;
 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainBottomA.size); // Grain-Wire contact count
 buffer<float> Fx(GBcc), Fy(GBcc), Fz(GBcc);
 buffer<float> TAx(GBcc), TAy(GBcc), TAz(GBcc);
 grainBottomEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    v8ui A = *(v8ui*)(grainBottomA.data+i);
    // FIXME: Recomputing from intersection (more efficient than storing?)
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
    v8sf depth = float8(Grain::radius) - gather(grain.Pz, A);
    // Gather static frictions
    v8sf localAx = *(v8sf*)(grainBottomLocalAx.data+i);
    v8sf localAy = *(v8sf*)(grainBottomLocalAy.data+i);
    v8sf localAz = *(v8sf*)(grainBottomLocalAz.data+i);
    v8sf localBx = *(v8sf*)(grainBottomLocalBx.data+i);
    v8sf localBy = *(v8sf*)(grainBottomLocalBy.data+i);
    v8sf localBz = *(v8sf*)(grainBottomLocalBz.data+i);
    // FIXME: inline full SIMD
    contact<Grain, Obstacle>(grain, A, depth,
                             _0f, _0f, float8(-Grain::radius),
                             _0f, _0f, _1f,
                             Ax, Ay, Az,
                             localAx, localAy, localAz,
                             localBx, localBy, localBz,
                             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                             *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i]
                             );
    // Store static frictions
    *(v8sf*)(grainBottomLocalAx.data+i) = localAx;
    *(v8sf*)(grainBottomLocalAy.data+i) = localAy;
    *(v8sf*)(grainBottomLocalAz.data+i) = localAz;
    *(v8sf*)(grainBottomLocalBx.data+i) = localBx;
    *(v8sf*)(grainBottomLocalBy.data+i) = localBy;
    *(v8sf*)(grainBottomLocalBz.data+i) = localBz;
   }
 }, 1);

 grainBottomSumTime.start();
 for(size_t i = 0; i < grainBottomA.size; i++) { // Scalar scatter add
  size_t a = grainBottomA[i];
  grain.Fx[a] += Fx[i];
  grain.Fy[a] += Fy[i];
  grain.Fz[a] += Fz[i];
  grain.Tx[a] += TAx[i];
  grain.Ty[a] += TAy[i];
  grain.Tz[a] += TAz[i];
 }
 grainBottomSumTime.stop();
}
