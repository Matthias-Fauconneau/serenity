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

  atomic contactCount;
  auto search = [&](uint, size_t start, size_t size) {
   const float* const gPz = grain.Pz.data+simd;
   const vXsf _bZ_Gr = floatX(bottomZ+Grain::radius);
   int* const gbA = grainBottomA.begin();
   for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
     vXsf Az = load(gPz, i);
     vXsf depth = _bZ_Gr - Az;
     maskX contact = greaterThanOrEqual(depth, _0f);
     uint index = contactCount.fetchAdd(countBits(contact));
     compressStore(gbA+index, contact, intX(i)+_seqi);
   }
  };
  if(grain.count/simd) grainBottomSearchTime += parallel_chunk(grain.count/simd, search);
  if(grain.count%simd) search(0, grain.count/simd, 1u);
  grainBottomA.size = contactCount;
  while(contactCount.count > 0 && grainBottomA[contactCount-1] >= (int)grain.count) contactCount.count--;
  grainBottomA.size = contactCount;
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

  assert(align(simd, grainBottomA.size+1) <= grainBottomA.capacity);
  for(size_t i=grainBottomA.size; i<align(simd, grainBottomA.size+1); i++)
   grainBottomA.begin()[i] = grain.count;
 }
 if(!grainBottomA) return;

 // Filters verlet lists, packing contacts to evaluate
 if(align(simd, grainBottomA.size) > grainBottomContact.capacity) {
  grainBottomContact = buffer<int>(align(simd, grainBottomA.size));
 }
 grainBottomContact.size = 0;

 // Creates a map from packed contact to index into unpacked contact list (indirect reference)
 // Instead of packing (copying) the unpacked list to a packed contact list
 // To keep track of where to write back (unpacked) contact positions (for static friction)
 // At the cost of requiring gathers (AVX2 (Haswell), MIC (Xeon Phi))
 atomic contactCount;
 auto filter = [&](uint, uint start, uint size) {
  const int* const gbA = grainBottomA.data;
  const float* const gPz = grain.Pz.data+simd;
  float* const gbL = grainBottomLocalAx.begin();
  int* const gbContact = grainBottomContact.begin();
  const vXsf _bZ_Gr = floatX(bottomZ+Grain::radius);
  for(uint i=start*simd; i<(start+size)*simd; i+=simd) {
    vXsi A = load(gbA, i);
    vXsf Az = gather(gPz, A);
    vXsf depth = _bZ_Gr - Az;
    maskX contact = greaterThanOrEqual(depth, _0f);
    maskStore(gbL+i, ~contact, _0f);
    uint index = contactCount.fetchAdd(countBits(contact));
    compressStore(gbContact+index, contact, intX(i)+_seqi);
  }
 };
 if(grainBottomA.size/simd) grainBottomFilterTime += parallel_chunk(grainBottomA.size/simd, filter);
 // The partial iteration has to be executed last so that invalid contacts are trailing
 // and can be easily trimmed
 if(grainBottomA.size%simd != 0) filter(0, grainBottomA.size/simd, 1u);
 grainBottomContact.size = contactCount;
 while(contactCount.count > 0 && grainBottomContact[contactCount.count-1] >= (int)grainBottomA.size)
  contactCount.count--; // Trims trailing invalid contacts
 grainBottomContact.size = contactCount;
 if(!grainBottomContact.size) return;
 for(uint i=grainBottomContact.size; i<align(simd, grainBottomContact.size); i++)
  grainBottomContact.begin()[i] = grainBottomA.size;

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
 constexpr float E = 1/((1-sq(Grain::poissonRatio))/Grain::elasticModulus+(1-sq(Plate::poissonRatio))/Plate::elasticModulus);
 constexpr float R = 1/(Grain::curvature/*+Plate::curvature*/);
 const float K = 4./3*E*sqrt(R);
 constexpr float mass = 1/(1/Grain::mass/*+1/Plate::mass*/);
 const float Kb = 2 * normalDampingRate * sqrt(2 * sqrt(R) * E * mass);
 grainBottomEvaluateTime += parallel_chunk(GBcc/simd, [&](uint, int start, int size) {
   evaluateGrainObstacle<false>(start, size,
                     grainBottomContact.data,
                     grainBottomA.data,
                     grain.Px.data+simd, grain.Py.data+simd, grain.Pz.data+simd,
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
