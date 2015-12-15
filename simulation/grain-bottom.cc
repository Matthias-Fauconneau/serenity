// TODO: Verlet
#include "simulation.h"
#include "parallel.h"
#include "grain-obstacle.h"

void Simulation::stepGrainBottom() {
 if(!grain.count) return;
 { // TODO: verlet
  swap(oldGrainBottomA, grainBottomA);
  swap(oldGrainBottomLocalAx, grainBottomLocalAx);
  swap(oldGrainBottomLocalAy, grainBottomLocalAy);
  swap(oldGrainBottomLocalAz, grainBottomLocalAz);
  swap(oldGrainBottomLocalBx, grainBottomLocalBx);
  swap(oldGrainBottomLocalBy, grainBottomLocalBy);
  swap(oldGrainBottomLocalBz, grainBottomLocalBz);

  static constexpr size_t averageGrainBottomContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainBottomContactCount +1);
  if(GBcc > grainBottomA.capacity) {
   memoryTime.start();
   grainBottomA = buffer<int>(GBcc, 0);
   grainBottomLocalAx = buffer<float>(GBcc, 0);
   grainBottomLocalAy = buffer<float>(GBcc, 0);
   grainBottomLocalAz = buffer<float>(GBcc, 0);
   grainBottomLocalBx = buffer<float>(GBcc, 0);
   grainBottomLocalBy = buffer<float>(GBcc, 0);
   grainBottomLocalBz = buffer<float>(GBcc, 0);
   memoryTime.stop();
  }
  grainBottomA.size = 0;

  //atomic contactCount;
  auto search = [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
    float depth = (bottomZ+Grain::radius) - grain.Pz[i];
    if(depth >= 0) grainBottomA.appendAtomic(i); // Grain
   }
  };
  grainBottomSearchTime += parallel_chunk(grain.count, search);
  grainBottomLocalAx.size = grainBottomA.size;
  grainBottomLocalAy.size = grainBottomA.size;
  grainBottomLocalAz.size = grainBottomA.size;
  grainBottomLocalBx.size = grainBottomA.size;
  grainBottomLocalBy.size = grainBottomA.size;
  grainBottomLocalBz.size = grainBottomA.size;
  if(!grainBottomA.size) return;
  //if(!contactCount) return;

  grainBottomRepackFrictionTime.start();
  size_t grainBottomI = 0; // Index of first contact with A in old grainBottom[Local]A|B list
  for(uint i=0; i<grainBottomA.size; i++) { // seq
   size_t j = grainBottomI;
   if(grainBottomI < oldGrainBottomA.size && (uint)oldGrainBottomA[grainBottomI] == i) {
    // Repack existing friction
    grainBottomLocalAx[i] = oldGrainBottomLocalAx[j];
    grainBottomLocalAy[i] = oldGrainBottomLocalAy[j];
    grainBottomLocalAz[i] = oldGrainBottomLocalAz[j];
    grainBottomLocalBx[i] = oldGrainBottomLocalBx[j];
    grainBottomLocalBy[i] = oldGrainBottomLocalBy[j];
    grainBottomLocalBz[i] = oldGrainBottomLocalBz[j];
   } else { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainBottomLocalAx[i] = 0;
   }
   while(grainBottomI < oldGrainBottomA.size && (uint)oldGrainBottomA[grainBottomI] == i)
    grainBottomI++;
  }
  grainBottomRepackFrictionTime.stop();

  for(size_t i=grainBottomA.size; i<align(simd, grainBottomA.size+1); i++)
   grainBottomA.begin()[i] = 0;
 }
 if(!grainBottomA) return;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainBottomA.size) > grainBottomContact.capacity) {
  grainBottomContact = buffer<int>(align(simd, grainBottomA.size));
 }
 grainBottomContact.size = 0;
 auto filter = [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   vXsi A = *(vXsi*)(grainBottomA.data+i);
   vXsf Az = gather(grain.Pz.data, A);
   vXsf depth = floatX(bottomZ+Grain::radius) - Az;
   for(size_t k: range(simd)) { // TODO: SIMD
    size_t j = i+k;
    if(j == grainBottomA.size) break /*2*/;
    if(extract(depth, k) >= 0) {
     // Creates a map from packed contact to index into unpacked contact list (indirect reference)
     // Instead of packing (copying) the unpacked list to a packed contact list
     // To keep track of where to write back (unpacked) contact positions (for static friction)
     // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
     grainBottomContact.appendAtomic( j );
    } else {
     error(extract(depth, k));
     // Resets contact (static friction spring)
     grainBottomLocalAx[j] = 0;
    }
   }
  }
 };
 grainBottomFilterTime += parallel_chunk(align(simd, grainBottomA.size)/simd, filter);
 for(size_t i=grainBottomContact.size; i<align(simd, grainBottomContact.size); i++)
  grainBottomContact.begin()[i] = grainBottomA.size;
 if(!grainBottomContact) return;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GBcc = align(simd, grainBottomContact.size); // Grain-Bottom contact count
 if(GBcc > grainBottomFx.capacity) {
  memoryTime.start();
  grainBottomFx = buffer<float>(GBcc);
  grainBottomFy = buffer<float>(GBcc);
  grainBottomFz = buffer<float>(GBcc);
  grainBottomTAx = buffer<float>(GBcc);
  grainBottomTAy = buffer<float>(GBcc);
  grainBottomTAz = buffer<float>(GBcc);
  memoryTime.stop();
 }
 grainBottomFx.size = GBcc;
 grainBottomFy.size = GBcc;
 grainBottomFz.size = GBcc;
 grainBottomTAx.size = GBcc;
 grainBottomTAy.size = GBcc;
 grainBottomTAz.size = GBcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Obstacle::poissonRatio))/Obstacle::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Obstacle::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Obstacle::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainBottomEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, size_t start, size_t size) {
   evaluateGrainObstacle<false>(start, size,
                     grainBottomContact.data, grainBottomContact.size,
                     grainBottomA.data,
                     grain.Px.data, grain.Py.data, grain.Pz.data,
                     floatX(bottomZ+Grain::radius), floatX(Grain::radius),
                     grainBottomLocalAx.begin(), grainBottomLocalAy.begin(), grainBottomLocalAz.begin(),
                     grainBottomLocalBx.begin(), grainBottomLocalBy.begin(), grainBottomLocalBz.begin(),
                     floatX(K), floatX(Kb),
                     floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                     floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                     grain.Vx.data, grain.Vy.data, grain.Vz.data,
                     grain.AVx.data, grain.AVy.data, grain.AVz.data,
                     grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                     grainBottomFx.begin(), grainBottomFy.begin(), grainBottomFz.begin(),
                     grainBottomTAx.begin(), grainBottomTAy.begin(), grainBottomTAz.begin() );
 });

 grainBottomSumTime.start();
 float bottomForceZ = 0;
 for(size_t i=0; i<grainBottomContact.size; i++) { // Scalar scatter add
  size_t index = grainBottomContact[i];
  assert(index == i, i, index);
  size_t a = grainBottomA[index];
  grain.Fx[a] += grainBottomFx[i];
  grain.Fy[a] += grainBottomFy[i];
  grain.Fz[a] += grainBottomFz[i];
  grain.Tx[a] += grainBottomTAx[i];
  grain.Ty[a] += grainBottomTAy[i];
  grain.Tz[a] += grainBottomTAz[i];
  bottomForceZ += grainBottomFz[i];
 }
 this->bottomForceZ = bottomForceZ;
 grainBottomSumTime.stop();
}
