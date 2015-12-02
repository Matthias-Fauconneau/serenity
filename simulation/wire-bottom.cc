// TODO: Verlet
#include "simulation.h"
#include "parallel.h"

void Simulation::stepWireBottom() {
 {
  swap(oldWireBottomA, wireBottomA);
  swap(oldWireBottomLocalAx, wireBottomLocalAx);
  swap(oldWireBottomLocalAy, wireBottomLocalAy);
  swap(oldWireBottomLocalAz, wireBottomLocalAz);
  swap(oldWireBottomLocalBx, wireBottomLocalBx);
  swap(oldWireBottomLocalBy, wireBottomLocalBy);
  swap(oldWireBottomLocalBz, wireBottomLocalBz);

  static constexpr size_t averageWireBottomContactCount = 1;
  size_t WBcc = align(simd, wire.count * averageWireBottomContactCount);
  if(WBcc > wireBottomA.capacity) {
   wireBottomA = buffer<uint>(WBcc, 0);
   wireBottomLocalAx = buffer<float>(WBcc, 0);
   wireBottomLocalAy = buffer<float>(WBcc, 0);
   wireBottomLocalAz = buffer<float>(WBcc, 0);
   wireBottomLocalBx = buffer<float>(WBcc, 0);
   wireBottomLocalBy = buffer<float>(WBcc, 0);
   wireBottomLocalBz = buffer<float>(WBcc, 0);
  }
  wireBottomA.size = 0;
  wireBottomLocalAx.size = 0;
  wireBottomLocalAy.size = 0;
  wireBottomLocalAz.size = 0;
  wireBottomLocalBx.size = 0;
  wireBottomLocalBy.size = 0;
  wireBottomLocalBz.size = 0;

  size_t wireBottomI = 0; // Index of first contact with A in old wireBottom[Local]A|B list
  wireBottomFilterTime += parallel_chunk(wire.count, [&](uint, size_t start, size_t size) {
   for(size_t i=start; i<(start+size); i+=1) { // TODO: SIMD
     if(wire.Pz[i] > Wire::radius) continue;
     wireBottomA.append( i ); // Wire
     size_t j = wireBottomI;
     if(wireBottomI < oldWireBottomA.size && oldWireBottomA[wireBottomI] == i) {
      // Repack existing friction
      wireBottomLocalAx.append( oldWireBottomLocalAx[j] );
      wireBottomLocalAy.append( oldWireBottomLocalAy[j] );
      wireBottomLocalAz.append( oldWireBottomLocalAz[j] );
      wireBottomLocalBx.append( oldWireBottomLocalBx[j] );
      wireBottomLocalBy.append( oldWireBottomLocalBy[j] );
      wireBottomLocalBz.append( oldWireBottomLocalBz[j] );
     } else { // New contact
      // Appends zero to reserve slot. Zero flags contacts for evaluation.
      // Contact points (for static friction) will be computed during force evaluation (if fine test passes)
      wireBottomLocalAx.append( 0 );
      wireBottomLocalAy.append( 0 );
      wireBottomLocalAz.append( 0 );
      wireBottomLocalBx.append( 0 );
      wireBottomLocalBy.append( 0 );
      wireBottomLocalBz.append( 0 );
     }
     while(wireBottomI < oldWireBottomA.size && oldWireBottomA[wireBottomI] == i)
      wireBottomI++;
    }
  }, 1);

  for(size_t i=wireBottomA.size; i<align(simd, wireBottomA.size); i++) wireBottomA.begin()[i] = 0;
 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)
 const size_t WBcc = align(simd, wireBottomA.size); // Wire-Bottom contact count
 buffer<float> Fx(WBcc), Fy(WBcc), Fz(WBcc);
 buffer<float> TAx(WBcc), TAy(WBcc), TAz(WBcc);
 wireBottomEvaluateTime.start();
 for(size_t index = 0; index < WBcc; index += 8) { // FIXME: parallel
  v8ui A = *(v8ui*)(wireBottomA.data+index);
  v8sf Ax = gather(wire.Px, A), Ay = gather(wire.Py, A), Az = gather(wire.Pz, A);
  v8sf depth = float8(Wire::radius) - gather(wire.Pz, A);
  // Gather static frictions
  v8sf localAx = *(v8sf*)(wireBottomLocalAx.data+index);
  v8sf localAy = *(v8sf*)(wireBottomLocalAy.data+index);
  v8sf localAz = *(v8sf*)(wireBottomLocalAz.data+index);
  v8sf localBx = *(v8sf*)(wireBottomLocalBx.data+index);
  v8sf localBy = *(v8sf*)(wireBottomLocalBy.data+index);
  v8sf localBz = *(v8sf*)(wireBottomLocalBz.data+index);
  contact<Wire, Obstacle>(wire, A, depth,
                      _0f, _0f, float8(-Wire::radius),
                      _0f, _0f, _1f,
                      Ax, Ay, Az,
                      localAx, localAy, localAz,
                      localBx, localBy, localBz,
                      *(v8sf*)&Fx[index], *(v8sf*)&Fy[index], *(v8sf*)&Fz[index]);
  // Scatter static frictions
  *(v8sf*)(wireBottomLocalAx.data+index) = localAx;
  *(v8sf*)(wireBottomLocalAy.data+index) = localAy;
  *(v8sf*)(wireBottomLocalAz.data+index) = localAz;
  *(v8sf*)(wireBottomLocalBx.data+index) = localBx;
  *(v8sf*)(wireBottomLocalBy.data+index) = localBy;
  *(v8sf*)(wireBottomLocalBz.data+index) = localBz;
 }
 wireBottomEvaluateTime.stop();

 wireBottomSumTime.start();
 for(size_t index = 0; index < wireBottomA.size; index++) { // Scalar scatter add
  size_t a = wireBottomA[index];
  wire.Fx[a] += Fx[index];
  wire.Fy[a] += Fy[index];
  wire.Fz[a] += Fz[index];
 }
 wireBottomSumTime.stop();
}
