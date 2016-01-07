// TODO: Verlet
#include "simulation.h"
#include "parallel.h"
#include "grain-obstacle.h"
#include "grain.h"
#include "plate.h"

void Simulation::stepGrainTop() {
 if(!grain->count) return;
 {
  swap(oldGrainTopA, grainTopA);
  swap(oldGrainTopLocalAx, grainTopLocalAx);
  swap(oldGrainTopLocalAy, grainTopLocalAy);
  swap(oldGrainTopLocalAz, grainTopLocalAz);
  swap(oldGrainTopLocalBx, grainTopLocalBx);
  swap(oldGrainTopLocalBy, grainTopLocalBy);
  swap(oldGrainTopLocalBz, grainTopLocalBz);

  static constexpr size_t averageGrainTopContactCount = 1;
  const size_t GTcc = align(simd, grain->count * averageGrainTopContactCount +1);
  if(GTcc > grainTopA.capacity) {
   grainTopA = buffer<int>(GTcc, 0);
   grainTopLocalAx = buffer<float>(GTcc, 0);
   grainTopLocalAy = buffer<float>(GTcc, 0);
   grainTopLocalAz = buffer<float>(GTcc, 0);
   grainTopLocalBx = buffer<float>(GTcc, 0);
   grainTopLocalBy = buffer<float>(GTcc, 0);
   grainTopLocalBz = buffer<float>(GTcc, 0);
  }
  grainTopA.size = 0;

  atomic contactCount;
  auto search = [&](uint, size_t start, size_t size) {
   const float* const gPz = grain->Pz.data+simd;
   const vXsf tZ_Gr = floatX(topZ-Grain::radius);
   int* const gtA = grainTopA.begin();
   for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     vXsf Az = load(gPz, i);
     maskX contact = greaterThanOrEqual(Az, tZ_Gr);
     uint index = contactCount.fetchAdd(countBits(contact));
     compressStore(gtA+index, contact, intX(i)+_seqi);
   }
  };
  if(grain->count/simd) grainTopSearchTime += parallel_chunk(grain->count/simd, search);
  if(grain->count%simd) search(0, grain->count/simd, 1u);
  grainTopA.size = contactCount;
  while(contactCount.count > 0 && grainTopA[contactCount-1] >= (int)grain->count) contactCount.count--;
  grainTopA.size = contactCount;
  grainTopLocalAx.size = grainTopA.size;
  grainTopLocalAy.size = grainTopA.size;
  grainTopLocalAz.size = grainTopA.size;
  grainTopLocalBx.size = grainTopA.size;
  grainTopLocalBy.size = grainTopA.size;
  grainTopLocalBz.size = grainTopA.size;
  if(!grainTopA.size) return;

  grainTopRepackFrictionTime.start();
  size_t grainTopI = 0; // Index of first contact with A in old grainTop[Local]A|B list
  for(uint i=0; i<grainTopA.size; i++) { // seq
   size_t j = grainTopI;
   if(grainTopI < oldGrainTopA.size && (uint)oldGrainTopA[grainTopI] == i) {
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
   while(grainTopI < oldGrainTopA.size && (uint)oldGrainTopA[grainTopI] == i)
    grainTopI++;
  }
  grainTopRepackFrictionTime.stop();

  for(size_t i=grainTopA.size; i<align(simd, grainTopA.size+1); i++)
   grainTopA.begin()[i] = -1;
 }
 if(!grainTopA) return;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainTopA.size) > grainTopContact.capacity) {
  grainTopContact = buffer<int>(align(simd, grainTopA.size));
 }
 grainTopContact.size = 0;
 // Creates a map from packed contact to index into unpacked contact list (indirect reference)
 // Instead of packing (copying) the unpacked list to a packed contact list
 // To keep track of where to write back (unpacked) contact positions (for static friction)
 // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
 atomic contactCount;
 auto filter = [&](uint, uint start, uint size) {
  const int* const gtA = grainTopA.data;
  const float* const gPz = grain->Pz.data+simd;
  float* const gtL = grainTopLocalAx.begin();
  int* const gtContact = grainTopContact.begin();
  const vXsf tZ_Gr = floatX(topZ-Grain::radius);
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsi A = load(gtA, i);
    vXsf Az = gather(gPz, A);
    maskX contact = greaterThanOrEqual(Az, tZ_Gr);
    maskStore(gtL+i, ~contact, _0f);
    uint index = contactCount.fetchAdd(countBits(contact));
    compressStore(gtContact+index, contact, intX(i)+_seqi);
  }
 };
 if(grainTopA.size/simd) grainTopFilterTime += parallel_chunk(grainTopA.size/simd, filter);
 // The partial iteration has to be executed last so that invalid contacts are trailing
 // and can be easily trimmed
 if(grainTopA.size%simd != 0) filter(0, grainTopA.size/simd, 1u);
 grainTopContact.size = contactCount;
 while(contactCount.count > 0 && grainTopContact[contactCount.count-1] >= (int)grainTopA.size)
  contactCount.count--; // Trims trailing invalid contacts
 grainTopContact.size = contactCount;
 if(!grainTopContact.size) return;
 for(uint i=grainTopContact.size; i<align(simd, grainTopContact.size); i++)
  grainTopContact.begin()[i] = grainTopA.size;

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
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Plate::poissonRatio))/Plate::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Obstacle::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Obstacle::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
#if MIDLIN
 const float Kt = 8 * 1/(1/Grain::shearModulus+1/Plate::shearModulus) * sqrt(R);
#endif
 grainTopEvaluateTime += parallel_chunk(GTcc/simd, [&](uint, size_t start, size_t size) {
   evaluateGrainObstacle<true>(start, size,
                     grainTopContact.data,
                     grainTopA.data,
                     grain->Px.data+simd, grain->Py.data+simd, grain->Pz.data+simd,
                     floatX(topZ-Grain::radius), floatX(Grain::radius),
                     grainTopLocalAx.begin(), grainTopLocalAy.begin(), grainTopLocalAz.begin(),
                     grainTopLocalBx.begin(), grainTopLocalBy.begin(), grainTopLocalBz.begin(),
                     floatX(K), floatX(Kb),
#if MIDLIN
                     floatX(Kt), floatX(dynamicGrainObstacleFrictionCoefficient),
#else
                     floatX(staticFrictionStiffness), floatX(dynamicGrainObstacleFrictionCoefficient),
                     floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
#endif
                     //floatX(staticFrictionLength), floatX(staticFrictionSpeed), floatX(staticFrictionDamping),
                     grain->Vx.data+simd, grain->Vy.data+simd, grain->Vz.data+simd,
                     grain->AVx.data+simd, grain->AVy.data+simd, grain->AVz.data+simd,
                     grain->Rx.data+simd, grain->Ry.data+simd, grain->Rz.data+simd, grain->Rw.data+simd,
                     grainTopFx.begin(), grainTopFy.begin(), grainTopFz.begin(),
                     grainTopTAx.begin(), grainTopTAy.begin(), grainTopTAz.begin() );
 });

 grainTopSumTime.start();
 float topForceZ = 0;
 for(size_t i=0; i<grainTopContact.size; i++) { // Scalar scatter add
  size_t index = grainTopContact[i];
  size_t a = grainTopA[index];
  grain->Fx[simd+a] += grainTopFx[i];
  grain->Fy[simd+a] += grainTopFy[i];
  grain->Fz[simd+a] += grainTopFz[i];
  //assert_(grainTopFz[i] > -400*N, a, grainTopFz[i], grain->Pz[simd+a], topZ-Grain::radius);
  grain->Tx[simd+a] += grainTopTAx[i];
  grain->Ty[simd+a] += grainTopTAy[i];
  grain->Tz[simd+a] += grainTopTAz[i];
  topForceZ -= grainTopFz[i];
 }
 this->topForceZ = topForceZ; // FIXME: average
 topSumStepCount++;
 grainTopSumTime.stop();
}
