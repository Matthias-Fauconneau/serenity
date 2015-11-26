// TODO: Verlet
#include "simulation.h"

bool Simulation::stepWireBottom() {
 {
  // SoA (FIXME: single pointer/index)
  static constexpr size_t averageWireBottomContactCount = 1;
  size_t WBcc = align(simd, wire.count * averageWireBottomContactCount);
  buffer<uint> wireBottomA           (WBcc, 0);
  buffer<float> wireBottomLocalAx (WBcc, 0);
  buffer<float> wireBottomLocalAy (WBcc, 0);
  buffer<float> wireBottomLocalAz (WBcc, 0);
  buffer<float> wireBottomLocalBx (WBcc, 0);
  buffer<float> wireBottomLocalBy (WBcc, 0);
  buffer<float> wireBottomLocalBz (WBcc, 0);

  size_t wireBottomI = 0; // Index of first contact with A in old wireBottom[Local]A|B list
  for(size_t a: range(wire.count)) { // TODO: SIMD
   if(wire.Pz[a] > Wire::radius) continue;
   wireBottomA.append( a ); // Wire
   size_t j = wireBottomI;
   if(wireBottomI < this->wireBottomA.size && this->wireBottomA[wireBottomI] == a) {
    // Repack existing friction
    wireBottomLocalAx.append( this->wireBottomLocalAx[j] );
    wireBottomLocalAy.append( this->wireBottomLocalAy[j] );
    wireBottomLocalAz.append( this->wireBottomLocalAz[j] );
    wireBottomLocalBx.append( this->wireBottomLocalBx[j] );
    wireBottomLocalBy.append( this->wireBottomLocalBy[j] );
    wireBottomLocalBz.append( this->wireBottomLocalBz[j] );
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
   while(wireBottomI < this->wireBottomA.size && this->wireBottomA[wireBottomI] == a)
    wireBottomI++;
  }

  for(size_t i=wireBottomA.size; i<WBcc; i++) wireBottomA.begin()[i] = 0;
  this->wireBottomA = move(wireBottomA);
  this->wireBottomLocalAx = move(wireBottomLocalAx);
  this->wireBottomLocalAy = move(wireBottomLocalAy);
  this->wireBottomLocalAz = move(wireBottomLocalAz);
  this->wireBottomLocalBx = move(wireBottomLocalBx);
  this->wireBottomLocalBy = move(wireBottomLocalBy);
  this->wireBottomLocalBz = move(wireBottomLocalBz);
 }

 // TODO: verlet

 // Evaluates forces from (packed) intersections (SoA)

 const size_t WBcc = align(simd, wireBottomA.size); // Wire-Bottom contact count

 buffer<float> Fx(WBcc), Fy(WBcc), Fz(WBcc);
 buffer<float> TAx(WBcc), TAy(WBcc), TAz(WBcc);
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

 for(size_t index = 0; index < wireBottomA.size; index++) { // Scalar scatter add
  size_t a = wireBottomA[index];
  wire.Fx[a] += Fx[index];
  wire.Fy[a] += Fy[index];
  wire.Fz[a] += Fz[index];
 }

 return true;
}
