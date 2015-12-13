// TODO: Verlet
#include "simulation.h"
#include "parallel.h"
#include "grain-obstacle.h"

void Simulation::stepGrainTop() {
 if(!grain.count) return;
 {
  swap(oldGrainTopA, grainTopA);
  swap(oldGrainTopLocalAx, grainTopLocalAx);
  swap(oldGrainTopLocalAy, grainTopLocalAy);
  swap(oldGrainTopLocalAz, grainTopLocalAz);
  swap(oldGrainTopLocalBx, grainTopLocalBx);
  swap(oldGrainTopLocalBy, grainTopLocalBy);
  swap(oldGrainTopLocalBz, grainTopLocalBz);

  static constexpr size_t averageGrainTopContactCount = 1;
  const size_t GBcc = align(simd, grain.count * averageGrainTopContactCount +1);
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
/*
  size_t grainTopI = 0; // Index of first contact with A in old grainTop[Local]A|B list
  auto search = [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
    if(grain.Pz[i] < topZ-Grain::radius) continue; // Ref
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
  };
  grainTopFilterTime += parallel_chunk(grain.count, search, 1 /*FIXME*/);

  //atomic contactCount;
  auto search = [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
    float depth = (topZ+Grain::radius) - grain.Pz[i]; // FIXME
    if(depth >= 0) grainTopA.appendAtomic(i); // Grain
   }
  };
  grainTopSearchTime += parallel_chunk(grain.count, search);
  grainTopLocalAx.size = grainTopA.size;
  grainTopLocalAy.size = grainTopA.size;
  grainTopLocalAz.size = grainTopA.size;
  grainTopLocalBx.size = grainTopA.size;
  grainTopLocalBy.size = grainTopA.size;
  grainTopLocalBz.size = grainTopA.size;
  if(!grainTopA.size) return;
  //if(!contactCount) return;

  grainTopRepackFrictionTime.start();
  size_t grainTopI = 0; // Index of first contact with A in old grainTop[Local]A|B list
  for(uint i=0; i<grainTopA.size; i++) { // seq
   size_t j = grainTopI;
   if(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i) {
    // Repack existing friction
    grainTopLocalAx[i] = oldGrainTopLocalAx[j];
    grainTopLocalAy[i] = oldGrainTopLocalAy[j];
    grainTopLocalAz[i] = oldGrainTopLocalAz[j];
    grainTopLocalBx[i] = oldGrainTopLocalBx[j];
    grainTopLocalBy[i] = oldGrainTopLocalBy[j];
    grainTopLocalBz[i] = oldGrainTopLocalBz[j];
   } else { // New contact
    // Appends zero to reserve slot. Zero flags contacts for evaluation.
    // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
    grainTopLocalAx[i] = 0;
   }
   while(grainTopI < oldGrainTopA.size && oldGrainTopA[grainTopI] == i)
    grainTopI++;
  }
  grainTopRepackFrictionTime.stop();

  for(size_t i=grainTopA.size; i<align(simd, grainTopA.size+1); i++)
   grainTopA.begin()[i] = 0;
 }
 if(!grainTopA) return;

 // TODO: verlet
 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainTopA.size) > grainTopContact.capacity) {
  grainTopContact = buffer<uint>(align(simd, grainTopA.size));
 }
 grainTopContact.size = 0;
 auto filter =  [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
   vXui A = *(vXui*)(grainTopA.data+i);
   vXsf Az = gather(grain.Pz.data, A);
   for(size_t k: range(simd)) {
    size_t j = i+k;
    if(j == grainTopA.size) break /*2*/;
    if(extract(Az, k) < topZ+Grain::radius) { // FIXME
     // Creates a map from packed contact to index into unpacked contact list (indirect reference)
     // Instead of packing (copying) the unpacked list to a packed contact list
     // To keep track of where to write back (unpacked) contact positions (for static friction)
     // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
     grainTopContact.append( j );
    } else {
     // Resets contact (static friction spring)
     grainTopLocalAx[j] = 0;
    }
   }
  }
 };
 grainTopFilterTime += parallel_chunk(align(simd, grainTopA.size)/simd, filter);
 for(size_t i=grainTopContact.size; i<align(simd, grainTopContact.size); i++)
  grainTopContact.begin()[i] = grainTopA.size;
 if(!grainTopContact) return;

 // Evaluates forces from (packed) intersections (SoA)
 size_t GTcc = align(simd, grainTopContact.size); // Grain-Wire contact count
 if(GTcc > grainTopFx.capacity) {
  grainTopFx = buffer<float>(GTcc);
  grainTopFy = buffer<float>(GTcc);
  grainTopFz = buffer<float>(GTcc);
  grainTopTAx = buffer<float>(GTcc);
  grainTopTAy = buffer<float>(GTcc);
  grainTopTAz = buffer<float>(GTcc);
 }
 grainTopFx.size = GTcc;
 grainTopFy.size = GTcc;
 grainTopFz.size = GTcc;
 grainTopTAx.size = GTcc;
 grainTopTAy.size = GTcc;
 grainTopTAz.size = GTcc;
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Obstacle::poissonRatio))/Obstacle::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Obstacle::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Obstacle::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainTopEvaluateTime += parallel_chunk(GTcc/simd, [&](uint, size_t start, size_t size) {
   evaluateGrainObstacle<true>(start, size,
                     grainTopContact.data, grainTopContact.size,
                     grainTopA.data,
                     grain.Px.data, grain.Py.data, grain.Pz.data,
                     floatX(topZ-Grain::radius), floatX(Grain::radius), // FIXME
                     grainTopLocalAx.begin(), grainTopLocalAy.begin(), grainTopLocalAz.begin(),
                     grainTopLocalBx.begin(), grainTopLocalBy.begin(), grainTopLocalBz.begin(),
                     floatX(K), floatX(Kb),
                     floatX(staticFrictionStiffness), floatX(dynamicFrictionCoefficient),
                     floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                     grain.Vx.data, grain.Vy.data, grain.Vz.data,
                     grain.AVx.data, grain.AVy.data, grain.AVz.data,
                     grain.Rx.data, grain.Ry.data, grain.Rz.data, grain.Rw.data,
                     grainTopFx.begin(), grainTopFy.begin(), grainTopFz.begin(),
                     grainTopTAx.begin(), grainTopTAy.begin(), grainTopTAz.begin() );
 });

 grainTopSumTime.start();
 float topForceZ = 0;
 for(size_t i=0; i<grainTopA.size; i++) { // Scalar scatter add
  size_t a = grainTopA[i];
  grain.Fx[a] += grainTopFx[i];
  grain.Fy[a] += grainTopFy[i];
  grain.Fz[a] += grainTopFz[i];
  grain.Tx[a] += grainTopTAx[i];
  grain.Ty[a] += grainTopTAy[i];
  grain.Tz[a] += grainTopTAz[i];
  topForceZ -= grainTopFz[i];
 }
 this->topForceZ = topForceZ;
 grainTopSumTime.stop();
}
