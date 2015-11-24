#include "system.h"
#include "list.h"
#include "lattice.h"
#include "grid.h"
#include "time.h"
#include "variant.h"

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Release, Error };
static string processStates[] {"pour", "release", "error"};

struct Simulation : System {
 // Process parameters
 const float radius;
 const float targetHeight = radius;
 const float patternRadius = radius - Grain::radius;
 const Pattern pattern;
 const float linearSpeed = 0.10 * m/s;
 const float verticalSpeed = 0.05 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = patternRadius;

 // Grain-Plate friction
 buffer<float> grainPlateLocalAx;
 buffer<float> grainPlateLocalAy;
 buffer<float> grainPlateLocalAz;
 buffer<float> grainPlateLocalBx;
 buffer<float> grainPlateLocalBy;
 buffer<float> grainPlateLocalBz;

 // Grain-Side friction
 buffer<float> grainSideLocalAx;
 buffer<float> grainSideLocalAy;
 buffer<float> grainSideLocalAz;
 buffer<float> grainSideLocalBx;
 buffer<float> grainSideLocalBy;
 buffer<float> grainSideLocalBz;

 // Grain - Grain
 float grainGrainGlobalMinD6 = 0;
 uint grainSkipped = 0;

 size_t grainGrainCount = 0;
 static constexpr size_t grainGrain = 12;
 buffer<int> grainGrainA {grain.capacity * grainGrain};
 buffer<int> grainGrainB {grain.capacity * grainGrain};
 // Grain-Grain Friction
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 // Grain - Wire
 // TODO: Verlet
 static constexpr size_t grainWire = 64;
 size_t grainWireCount = 0;
 buffer<int> grainWireA {wire.capacity};
 buffer<int> grainWireB {wire.capacity};
 // Grain-Wire Friction
 buffer<float> grainWireLocalAx;
 buffer<float> grainWireLocalAy;
 buffer<float> grainWireLocalAz;
 buffer<float> grainWireLocalBx;
 buffer<float> grainWireLocalBy;
 buffer<float> grainWireLocalBz;

 struct Force { vec3 origin, force; };
 array<Force> forces;

 Simulation(const Dict& p) :
   System(p.at("TimeStep")),
   radius(p.at("Radius")),
   pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
  if(pattern) { // Initial wire node
   size_t i = wire.count++;
   wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentPourHeight;
   wire.Vx[i] = 0;
   wire.Vy[i] = 0;
   wire.Vz[i] = 0;
  }
 }

 bool domain(vec3& min, vec3& max) {
  min = inf, max = -inf;
  // Grain
  // Avoids bound checks on grain-wire
  for(size_t i: range(grain.count)) {
   min.x = ::min(min.x, grain.Px[i]);
   max.x = ::max(max.x, grain.Px[i]);
   min.y = ::min(min.y, grain.Py[i]);
   max.y = ::max(max.y, grain.Py[i]);
   min.z = ::min(min.z, grain.Pz[i]);
   max.z = ::max(max.z, grain.Pz[i]);
  }
  if(!(min > vec3(-1) && max < vec3(1))) {
   log("Domain grain", min, max);
   processState = ProcessState::Fail;
   return false;
  }
  // Wire
  // Avoids bound checks on grain-wire
  for(size_t i: range(wire.count)) {
   min.x = ::min(min.x, wire.Px[i]);
   max.x = ::max(max.x, wire.Px[i]);
   min.y = ::min(min.y, wire.Py[i]);
   max.y = ::max(max.y, wire.Py[i]);
   min.z = ::min(min.z, wire.Pz[i]);
   max.z = ::max(max.z, wire.Pz[i]);
  }
  if(!(min > vec3(-1) && max < vec3(1))) {
   log("Domain wire", min, max);
   processState = ProcessState::Fail;
   return false;
  }
  return true;
 }

 unique<Lattice<uint16> > generateLattice(const Mass& vertices, float radius) {
  vec3 min, max;
  if(!domain(min, max)) return nullptr;
  unique<Lattice<uint16>> lattice(sqrt(3.)/(2*radius), min, max);
  for(size_t i: range(vertices.count))
   lattice->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]) = 1+i; // for grain-grain, grain-side, grain-wire
  return move(lattice);
 }

 unique<Grid> generateGrid(const Mass& vertices, float length) {
  vec3 min, max;
  if(!domain(min, max)) return nullptr;
  unique<Grid> grid(1/length, min, max);
  for(size_t i: range(vertices.count))
   grid->cell(vertices.Px[i], vertices.Py[i], vertices.Pz[i]).append(1+i); // for grain-grain, grain-side, grain-wire
  return move(grid);
 }

 bool step() {
  // Process
  if(currentHeight >= targetHeight || grain.count == grain.capacity) processState = Release;
  else {
   // Increases current height
   currentHeight += verticalSpeed * dt;
   // Generates falling grain (pour)
   if(currentPourHeight >= Grain::radius) {
    vec2 p(random()*2-1,random()*2-1);
    if(length(p)<1) { // Within cylinder
     vec3 newPosition (patternRadius*p.x, patternRadius*p.y, currentPourHeight);
     // Deposits grain without grain overlap
     for(size_t index: range(grain.count)) {
      vec3 other = grain.position(index);
      float Dxy = length((other - newPosition).xy());
      if(Dxy < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius) - sq(Dxy));
       newPosition.z = ::max(newPosition.z, other.z+dz);
      }
     }
     // Under current wire drop height
     if(newPosition.z < currentHeight) {
      // Without wire overlap
      for(size_t index: range(wire.count)) {
       vec3 p = wire.position(index);
       if(length(p - newPosition) < Grain::radius+Wire::radius)
        goto break_;
      }
      size_t i = grain.count;
      grain.Px[i] = newPosition.x; grain.Py[i] = newPosition.y; grain.Pz[i] = newPosition.z;
      grain.Vx[i] = 0; grain.Vy[i] = 0; grain.Vz[i] = 0;
      grain.AVx[i] = 0; grain.AVy[i] = 0; grain.AVz[i] = 0;
      float t0 = 2*PI*random();
      float t1 = acos(1-2*random());
      float t2 = (PI*random()+acos(random()))/2;
      grain.rotation[i] = (v4sf){sin(t0)*sin(t1)*sin(t2),
        cos(t0)*sin(t1)*sin(t2),
        cos(t1)*sin(t2),
        cos(t2)};
      grain.count++;
      // Forces lattice reevaluation
      grainGrainGlobalMinD6 = 0; grainSideGlobalMinD2 = 0;
     }
    }
   }
  }
  break_:;

  // Grain
  buffer<int> grainBottom(grain.count, 0), grainSide(grain.count, 0);
  for(size_t a: range(grain.count)) {
   // Initialization
   grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
   grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
   // Contact with obstacles (floor, side)
   if(grain.Pz[a]-Grain::radius < 0) grainBottom.append( a );
   if(processState == ProcessState::Pour &&
      sq(grain.Px[a]) + sq(grain.Py[a]) > sq(side.radius - Grain::radius)) {
    grainSide.append( a );
   }
  }
  {// Contact force with floor (SIMD)
   buffer<float> Fx(grainBottom.size), Fy(grainBottom.size), Fz(grainBottom.size);
   for(size_t i = 0; i < grainBottom.size; i += 8) {
    v8si A = *(v8si*)&grainBottom[i];
    v8sf depth = float8(Grain::radius) - gather(grain.Pz, A);
    contact<Grain, Obstacle>(grain, A, depth, _0f, _0f, -Grain::radius8, _0f, _0f, _1f,
            *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
   }
   for(size_t i = 0; i < grainBottom.size; i++) { // Scalar scatter add
    size_t a = grainBottom[i];
    assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
    grain.Fx[a] += Fx[i];
    grain.Fy[a] += Fy[i];
    grain.Fz[a] += Fz[i];
   }
  }
  float sideForce = 0;
  if(processState <= ProcessState::Pour) { // Rigid Side
   size_t gSC = grainRigidSideCount;
   while(gSC%8) grainRigidSide[gSC++] = grain.count;
   v8sf R = float8(side.radius-Grain::radius);
   buffer<float> Fx(gSC), Fy(gSC), Fz(gSC);
   for(size_t i = 0; i < gSC; i += 8) {
    v8si A = *(v8si*)&grainRigidSide[i];
    v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A);
    v8sf length = sqrt8(Ax*Ax + Ay*Ay);
    v8sf Nx = -Ax/length, Ny = -Ay/length;
    v8sf depth = length - R;
    contact<Grain, RigidSide>(grain, A, depth, Grain::radius8*(-Nx), Grain::radius8*(-Ny), _0f,
                              Nx, Ny, _0f,
                              *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
   }
   for(size_t i = 0; i < grainRigidSideCount; i++) { // Scalar scatter add
    size_t a = grainRigidSide[i];
    assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
    grain.Fx[a] += Fx[i];
    grain.Fy[a] += Fy[i];
    grain.Fz[a] += Fz[i];
    sideForce += (grain.Px[a]*Fx[i] + grain.Py[a]*Fy[i]) / sqrt(grain.Px[a]*grain.Px[a] + grain.Py[a]*grain.Py[a]); // dot(R^,F)
   }
  }
  grainTime.stop();

  unique<Lattice<uint16>> grainLattice = nullptr;
  unique<Grid> wireGrid = nullptr;
   //grainSideGlobalMinD2 = 0; // DEBUG
   if(grainSideGlobalMinD2 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
    grainSideLatticeTime.start();
    float minD3 = 2*Grain::radius/sqrt(3.); // - Grain::radius;
    grainSideCount = 0;
    if(!grainLattice) grainLattice = generateLattice(grain, Grain::radius);
    if(!grainLattice) return false;
    // Side - Grain
    const uint16* grainBase = grainLattice->base;
    int gX = grainLattice->size.x, gY = grainLattice->size.y;
    v4sf scale = grainLattice->scale, min = grainLattice->min;
    const int3 size = grainLattice->size;
    const uint16* grainNeighbours[3*3] = {
     grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
     grainBase-gX-1, grainBase-1, grainBase+gX-1,
     grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
    };
    //const v4sf XYZ0 = grainLattice->XYZ0;
    for(size_t i: range(1, side.H-1)) {
     for(size_t j: range(0, side.W)) {
      size_t s = 7+i*side.stride+j;
      v4sf O = side.position(s);
      //int offset = dot3(XYZ0, floor(scale*(O-min)))[0];
      int offset = int(scale[2]*(O[2]-min[2]))*(size.y*size.x) + int(scale[1]*(O[1]-min[1]))*size.x + int(scale[0]*(O[0]-min[0]));
      list<2> D;

      for(size_t n=0; n<3*3; n++) { // FIXME: assert unrolled
       v4hi line = *(v4hi*)(offset + grainNeighbours[n]); // Gather
       for(size_t i: range(3)) if(line[i]) { // FIXME: assert unrolled
        size_t g = line[i]-1;
        float d = sqrt(sq(side.Px[s]-grain.Px[g]) + sq(side.Py[s]-grain.Py[g]) + sq(side.Pz[s]-grain.Pz[g]));
        if(!D.insert(d, g)) minD3 = ::min(minD3, d);
       }
      }
      for(size_t i: range(D.size)) {
       grainSideA[grainSideCount] = D.elements[i].value;
       grainSideB[grainSideCount] = s;
       grainSideCount++;
      }
     }
    }
    //assert(minD3 < 2*Grain::radius/sqrt(3.) - Grain::radius, minD3);
    grainSideGlobalMinD2 = minD3 - Grain::radius;
    assert(grainSideGlobalMinD2 > 0, grainSideGlobalMinD2);
    // Aligns
    size_t gSC = grainSideCount;
    while(gSC%8) { // Content does not matter as long as intersection evaluation does not trigger exceptions
     grainSideA[gSC] = 0;
     grainSideB[gSC] = 0;
     gSC++;
    }
    assert(gSC <= grainSideA.capacity);
    grainSideLatticeTime.stop();
    //log("side", sideSkipped);
    sideSkipped=0;
   } else sideSkipped++;

   {
    // Evaluates (packed) intersections from (packed) verlet lists
    size_t grainSideContactCount = 0;
    buffer<int> grainSideContact(grainSideCount);
    grainSideIntersectTime.start();
    for(size_t index = 0; index < grainSideCount; index += 8) {
     v8si A = *(v8si*)(grainSideA.data+index), B = *(v8si*)(grainSideB.data+index);
     v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
     v8sf Bx = gather(side.Px.data, B), By = gather(side.Py.data, B), Bz = gather(side.Pz.data, B);
     v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
     v8sf depth = Grain::radius8 - length;
     for(size_t subIndex: range(8)) {
      if(index+subIndex == grainSideCount) break /*2*/;
      if(depth[subIndex] > 0) {
       grainSideContact[grainSideContactCount] = index+subIndex;
       grainSideContactCount++;
      }
     }
    }
    grainSideIntersectTime.stop();

    // Aligns with invalid contacts
    size_t gSCC = grainSideContactCount;
    while(gSCC%8)
     grainSideContact[gSCC++] = grainSideCount; // Invalid entry

    // Evaluates forces from (packed) intersections
    buffer<float> Fx(gSCC), Fy(gSCC), Fz(gSCC);
    grainSideForceTime.start();
    for(size_t i = 0; i < grainSideContactCount; i += 8) {
     v8si contacts = *(v8si*)(grainSideContact.data+i);
     v8si A = gather(grainSideA, contacts), B = gather(grainSideB, contacts);
     // FIXME: Recomputing from intersection (more efficient than storing?)
     v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
     v8sf Bx = gather(side.Px.data, B), By = gather(side.Py.data, B), Bz = gather(side.Pz.data, B);
     v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
     v8sf depth = Grain::radius8 - length;
     v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
     contact(grain, A, side, B, depth, -Rx, -Ry, -Rz, Nx, Ny, Nz,
             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
    }
    grainSideForceTime.stop();

    grainSideAddTime.start();
    for(size_t i = 0; i < grainSideContactCount; i++) { // Scalar scatter add
     size_t index = grainSideContact[i];
     size_t a = grainSideA[index];
     size_t b = grainSideB[index];
     grain.Fx[a] += Fx[i];
     side.Fx[b] -= Fx[i];
     grain.Fy[a] += Fy[i];
     side.Fy[b] -= Fy[i];
     grain.Fz[a] += Fz[i];
     side.Fz[b] -= Fz[i];
     sideForce += (grain.Px[a]*Fx[i] + grain.Py[a]*Fy[i]) / sqrt(grain.Px[a]*grain.Px[a] + grain.Py[a]*grain.Py[a]); // dot(R^,F)
     forces.append(side.position(b), -vec3(Fx[i], Fy[i], Fz[i]));
    }
    grainSideAddTime.stop();
   }
  }
  this->sideForce = sideForce;
#endif

  if(grainGrainGlobalMinD6 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
   grainGrainLatticeTime.start();
   buffer<int2> grainGrainIndices {grainCount8 * grainGrain};
   grainGrainIndices.clear();
   // Index to packed frictions
   for(size_t index: range(grainGrainCount)) {
    { // A<->B
     size_t a = grainGrainA[index];
     size_t base = a*grainGrain;
     size_t i = 0;
     while(grainGrainIndices[base+i]) i++;
     assert_(i<grainGrain, grainGrainIndices.slice(base, grainGrain));
     grainGrainIndices[base+i] = int2{grainGrainB[index]+1, int(index)};
    }
    /*{ // B<->A FIXME?
     size_t b = grainGrainB[index];
     size_t base = b*grainGrain;
     size_t i = 0;
     while(grainGrainIndices[base+i]) i++;
     assert(i<grainGrain, grainGrainIndices.slice(base, grainGrain));
     grainGrainIndices[base+i] = int2{grainGrainA[index]+1, int(index)};
    }*/
   }

   if(!grainLattice) grainLattice = generateLattice(grain, Grain::radius);
   if(!grainLattice) return false;
   float minD6 = 2*(2*Grain::radius/sqrt(3.)/*-Grain::radius*/);
   const int Z = grainLattice->size.z, Y = grainLattice->size.y, X = grainLattice->size.x;
   int di[62];
   size_t i = 0;
   for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
    di[i++] = z*Y*X + y*X + x;
   }
   assert(i==62);
   Locker lock(this->lock);
   grainGrainCount = 0;
   buffer<float> grainGrainLocalAx {grainCount8 * grainGrain};
   buffer<float> grainGrainLocalAy {grainCount8 * grainGrain};
   buffer<float> grainGrainLocalAz {grainCount8 * grainGrain};
   buffer<float> grainGrainLocalBx {grainCount8 * grainGrain};
   buffer<float> grainGrainLocalBy {grainCount8 * grainGrain};
   buffer<float> grainGrainLocalBz {grainCount8 * grainGrain};
   if(grainGrain <= 12) { // Grid || Exhaustive search
    for(size_t latticeIndex: range(Z*Y*X)) {
     const uint16* current = grainLattice->base + latticeIndex;
     size_t a = *current;
     if(!a) continue;
     a--;
     // Neighbours
     list<grainGrain/*12*/> D;
     for(size_t n: range(62)) {
      size_t b = current[di[n]];
      if(!b) continue;
      b--;
      float d = sqrt(sq(grain.Px[a]-grain.Px[b]) + sq(grain.Py[a]-grain.Py[b]) + sq(grain.Pz[a]-grain.Pz[b]));
      if(!D.insert(d, b)) minD6 = ::min(minD6, d);
     }
     assert_(minD6 > 0, minD6);
     //log(D.size, ref<element>(D.elements,D.size));
     for(size_t i: range(D.size)) {
      //assert(grainGrainCount < grainCount8 * grainGrain);
      size_t index = grainGrainCount;
      grainGrainA[index] = a;
      size_t B = D.elements[i].value;
      //assert(a!=B, a, B, i, D.size, D.elements);
      grainGrainB[index] = B;
      for(size_t i: range(grainGrain/*12*/)) {
       size_t b = grainGrainIndices[a*grainGrain+i][0];
       if(!b) break;
       b--;
       if(b == B) { // Repack existing friction
        size_t j = grainGrainIndices[a*grainGrain+i][1];
        grainGrainLocalAx[index] = this->grainGrainLocalAx[j];
        grainGrainLocalAy[index] = this->grainGrainLocalAy[j];
        grainGrainLocalAz[index] = this->grainGrainLocalAz[j];
        grainGrainLocalBx[index] = this->grainGrainLocalBx[j];
        grainGrainLocalBy[index] = this->grainGrainLocalBy[j];
        grainGrainLocalBz[index] = this->grainGrainLocalBz[j];
        goto break_;
       }
      } /*else*/ {
       grainGrainLocalAx[index] = 0; grainGrainLocalAy[index] = 0; grainGrainLocalAz[index] = 0;
       grainGrainLocalBx[index] = 0; grainGrainLocalBy[index] = 0; grainGrainLocalBz[index] = 0;
      }
      break_:;
      grainGrainCount++;
     }
    }
   } else {
    static bool unused once = ({ log("WARNING: Exhaustive Grain-Grain search enabled"); true; });
    assert_(grainGrain > 12);
    for(size_t a: range(grain.count)) {
     for(size_t b: range(a)) {
      size_t index = grainGrainCount;
      assert_(index < grainGrainA.size && index < grainGrainB.size);
      grainGrainA[index] = a;
      size_t B = b;
      assert(a!=B);
      grainGrainB[index] = B;
      grainGrainCount++;
     }
    }
   }

   //grainGrainCount2 = ::min(grainGrainCount2, grainGrainCount);
   this->grainGrainLocalAx = move(grainGrainLocalAx);
   this->grainGrainLocalAy = move(grainGrainLocalAy);
   this->grainGrainLocalAz = move(grainGrainLocalAz);
   this->grainGrainLocalBx = move(grainGrainLocalBx);
   this->grainGrainLocalBy = move(grainGrainLocalBy);
   this->grainGrainLocalBz = move(grainGrainLocalBz);
   //grainGrainCount2 = grainGrainCount;

   assert(grainGrainCount < grainCount8 * grainGrain);
   {// Aligns with invalid pairs
    size_t grainGrainCount = this->grainGrainCount;
    assert(grain.count < grain.capacity);
    // Need at least one invalid for packed
    grainGrainA[grainGrainCount] = grain.count;
    grainGrainB[grainGrainCount] = grain.count;
    grainGrainCount++;
    while(grainGrainCount%8) {
     grainGrainA[grainGrainCount] = grain.count;
     grainGrainB[grainGrainCount] = grain.count;
     grainGrainCount++;
    }
   }

   grainGrainGlobalMinD6 = minD6 - 2*Grain::radius;
   if(grainGrainGlobalMinD6 < 0) log("grainGrainGlobalMinD6", grainGrainGlobalMinD6);

   grainGrainLatticeTime.stop();
   //log("grain", grainSkipped);
   grainSkipped=0;
  } else grainSkipped++;

  grainGrainTime.start();
  // Evaluates (packed) intersections from (packed) verlet lists
  size_t grainGrainContactCount = 0;
  buffer<uint> grainGrainContact(grainCount8*grainGrain);
  for(size_t index = 0; index < grainGrainCount; index += 8) {
   v8si A = *(v8si*)(grainGrainA.data+index), B = *(v8si*)(grainGrainB.data+index);
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = (Grain::radius8+Grain::radius8) - length;
   for(size_t k: range(8)) {
    if(index+k < grainGrainCount && depth[k] > 0) {
      // Indirect instead of pack to write back friction points
     grainGrainContact[grainGrainContactCount] = index+k;
     grainGrainContactCount++;
    } else {
     grainGrainLocalAx[index+k] = 0; grainGrainLocalAy[index+k] = 0; grainGrainLocalAz[index+k] = 0;
     grainGrainLocalBx[index+k] = 0; grainGrainLocalBy[index+k] = 0; grainGrainLocalBz[index+k] = 0;
    }
   }
  }

  // Aligns with invalid contacts
  size_t gGCC = grainGrainContactCount;
  while(gGCC%8) grainGrainContact[gGCC++] = grainGrainCount /*invalid*/;

  // Evaluates forces from (packed) intersections
  buffer<float> Fx(gGCC), Fy(gGCC), Fz(gGCC);
  buffer<float> TAx(gGCC), TAy(gGCC), TAz(gGCC);
  buffer<float> TBx(gGCC), TBy(gGCC), TBz(gGCC);

  //for(size_t i = 0; i < grainGrainContactCount; i += 8) {
  parallel_chunk((grainGrainContactCount+7)/8, [&](uint, size_t start, size_t size) {
   for(size_t i=start*8; i < start*8+size*8; i+=8) {
   v8si contacts = *(v8si*)&grainGrainContact[i];
   v8si A = gather(grainGrainA, contacts), B = gather(grainGrainB, contacts);
   // FIXME: Recomputing from intersection (more efficient than storing?)
   v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
   v8sf Bx = gather(grain.Px, B), By = gather(grain.Py, B), Bz = gather(grain.Pz, B);
   v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
   v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
   v8sf depth = (Grain::radius8+Grain::radius8) - length;
   //for(size_t k: range(8)) if(i+k<grainGrainContactCount) assert(depth[k] >= 0, depth[k], A[k], B[k], i+k, grainGrainContact[i+k], grainGrainContactCount, gGCC);
   //log(grain.count, grainGrainCount, contacts, A, B, length);
   v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
   v8sf RBx = Grain::radius8 * Nx, RBy = Grain::radius8 * Ny, RBz = Grain::radius8 * Nz;
   v8sf RAx = -RBx, RAy = -RBy, RAz = -RBz;
   // Gather static frictions
   v8sf localAx = gather(grainGrainLocalAx, contacts);
   v8sf localAy = gather(grainGrainLocalAy, contacts);
   v8sf localAz = gather(grainGrainLocalAz, contacts);
   v8sf localBx = gather(grainGrainLocalBx, contacts);
   v8sf localBy = gather(grainGrainLocalBy, contacts);
   v8sf localBz = gather(grainGrainLocalBz, contacts);
   /*for(size_t k: range(8)) if(i+k<grainGrainContactCount)
    assert_(grain.AVx[A[k]] == 0, grain.AVx[A[k]]);*/
   contact<Grain, Grain>( grain, A, grain, B, depth,
                         RAx, RAy, RAz,
                         RBx, RBy, RBz,
                         Nx, Ny, Nz,
                         Ax, Ay, Az,
                         Bx, By, Bz,
                         localAx, localAy, localAz,
                         localBx, localBy, localBz,
                         *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                         *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i],
                         *(v8sf*)&TBx[i], *(v8sf*)&TBy[i], *(v8sf*)&TBz[i]
           );
   //for(size_t k: range(8)) if(i+k<grainGrainContactCount) assert_(TAx[i+k] == 0, TAx[i+k]);
   // Scatter static frictions
   scatter(grainGrainLocalAx, contacts, localAx);
   scatter(grainGrainLocalAy, contacts, localAy);
   scatter(grainGrainLocalAz, contacts, localAz);
   scatter(grainGrainLocalBx, contacts, localBx);
   scatter(grainGrainLocalBy, contacts, localBy);
   scatter(grainGrainLocalBz, contacts, localBz);
  }});

  for(size_t i = 0; i < grainGrainContactCount; i++) { // Scalar scatter add
   size_t index = grainGrainContact[i];
   size_t a = grainGrainA[index];
   size_t b = grainGrainB[index];
   grain.Fx[a] += Fx[i];
   grain.Fx[b] -= Fx[i];
   grain.Fy[a] += Fy[i];
   grain.Fy[b] -= Fy[i];
   grain.Fz[a] += Fz[i];
   grain.Fz[b] -= Fz[i];

   //assert_(TAx[i] == 0, TAx[i]);
   grain.Tx[a] += TAx[i];
   grain.Ty[a] += TAy[i];
   grain.Tz[a] += TAz[i];
   grain.Tx[b] += TBx[i];
   grain.Ty[b] += TBy[i];
   grain.Tz[b] += TBz[i];
  }
  grainGrainTime.stop();

  if(wire.count) {
   size_t wireCount8 = (wire.count+7)/8*8;
   buffer<int> wireBottom(wireCount8), wireTop(wireCount8); //, wireRigidSide(wireCount8);
   size_t wireBottomCount = 0, wireTopCount =0; //, wireRigidSideCount = 0;

   for(size_t B: range(wire.count)) { // TODO: SIMD
    // Gravity
    wire.Fx[B] = 0; wire.Fy[B] = 0; wire.Fz[B] = wire.mass * G.z;
    if(plate.Pz[0] > wire.Pz[B]-Wire::radius) wireBottom[wireBottomCount++] = B;
    if(wire.Pz[B]+Wire::radius > plate.Pz[1]) wireTop[wireTopCount++] = B;
   }
#if 1
   buffer<int2> grainWireIndices {grainCount8 * grainWire};
   grainWireIndices.clear(int2(0,0));
   // Index to packed frictions
   for(size_t index: range(grainWireCount)) {
    { // A<->B
     size_t a = grainWireA[index];
     assert_(a < grain.count, a, grain.count, index);
     size_t base = a*grainWire;
     size_t i = 0;
     while(i<grainWire && grainWireIndices[base+i]) i++;
     if(i>=grainWire) {
      log("i>=grainWire", "gWC index", index,  "/", grainWireCount, "a grain", a, "/", grain.count,  "base", base, "i", i/*, "gWI[base..+grainWire]", grainWireIndices.slice(base, grainWire)*/);
      //for(int2 gwc: grainWireIndices.slice(base, grainWire)) log("wire b", gwc[0], "contact #", gwc[1]);
      processState = ProcessState::Fail;
      highlightGrain.append( a );
      for(int2 gwc: grainWireIndices.slice(base, grainWire)) highlightWire.append( gwc[0] );
      return false;
     }
     assert_(i<grainWire, grainWireIndices.slice(base, grainWire));
     assert_(base+i < grainWireIndices.size);
     assert_((size_t)grainWireB[index] < wire.count, grainWireB[index], wire.count);
     assert_(index < grainWireCount, index, grainWireCount);
     grainWireIndices[base+i] = int2(grainWireB[index]+1, int(index)); // For each grain (a), maps wire (b) to grain-wire contact index
    }
   }
   grainWireCount = 0;

   if(!wireGrid) wireGrid = generateGrid(wire, Grain::radius+Wire::radius);
   if(!wireGrid) return false;
   uint16* wireBase = wireGrid->base;
   const int gX = wireGrid->size.x, gY = wireGrid->size.y;
   const uint16* wireNeighbours[3*3] = {
    wireBase+(-gX*gY-gX-1)*Grid::cellCapacity, wireBase+(-gX*gY-1)*Grid::cellCapacity, wireBase+(-gX*gY+gX-1)*Grid::cellCapacity,
    wireBase+(-gX-1)*Grid::cellCapacity, wireBase+(-1)*Grid::cellCapacity, wireBase+(gX-1)*Grid::cellCapacity,
    wireBase+(gX*gY-gX-1)*Grid::cellCapacity, wireBase+(gX*gY-1)*Grid::cellCapacity, wireBase+(gX*gY+gX-1)*Grid::cellCapacity
   };

   {
    //Locker lock(this->lock);
    buffer<float> grainWireLocalAx {grainCount8 * grainWire};
    buffer<float> grainWireLocalAy {grainCount8 * grainWire};
    buffer<float> grainWireLocalAz {grainCount8 * grainWire};
    buffer<float> grainWireLocalBx {grainCount8 * grainWire};
    buffer<float> grainWireLocalBy {grainCount8 * grainWire};
    buffer<float> grainWireLocalBz {grainCount8 * grainWire};

#if 0 // Grain lattice (FIXME: too many wire hits requires too large grainWire for grainWireIndices (fast lookup for grain-wire contacts conservation across verlet list updates)
    //buffer<size_t> found (16, 0);
    for(size_t B: range(wire.count)) { // TODO: SIMD
     // Wire - Grain (TODO: Verlet)
     //found.size = 0;
     {size_t offset = grainLattice->index(wire.Px[B], wire.Py[B], wire.Pz[B]); // offset
      for(size_t n: range(3*3)) {
       v4hi unused line = loada(grainNeighbours[n] + offset);
       for(size_t i: range(3)) if(line[i]) {
        assert_(grainWireCount < grainWireA.capacity, grainWireCount, grainWireA.capacity);
        size_t a = line[i]-1;
        //found.append(a);
        size_t index = grainWireCount;
        grainWireA[index] = a; // Grain
        grainWireB[index] = B; // Wire
        for(size_t i: range(grainWire)) {
         size_t b = grainWireIndices[a*grainWire+i][0]; // wire index + 1
         if(!b) break;
         b--;
         if(b == B) { // Repack existing friction
          size_t j = grainWireIndices[a*grainWire+i][1];
          grainWireLocalAx[index] = this->grainWireLocalAx[j];
          grainWireLocalAy[index] = this->grainWireLocalAy[j];
          grainWireLocalAz[index] = this->grainWireLocalAz[j];
          grainWireLocalBx[index] = this->grainWireLocalBx[j];
          grainWireLocalBy[index] = this->grainWireLocalBy[j];
          grainWireLocalBz[index] = this->grainWireLocalBz[j];
          goto break__;
         }
        } /*else*/ {
         grainWireLocalAx[index] = 0; grainWireLocalAy[index] = 0; grainWireLocalAz[index] = 0;
         grainWireLocalBx[index] = 0; grainWireLocalBy[index] = 0; grainWireLocalBz[index] = 0;
        }
        break__:;
        grainWireCount++;
       }
      }
     }
#else
    for(size_t a: range(grain.count)) { // TODO: SIMD
     // Wire - Grain (TODO: Verlet)
     {size_t offset = wireGrid->index(grain.Px[a], grain.Py[a], grain.Pz[a]); // offset
      for(size_t n: range(3*3)) for(size_t i: range(3)) {
       ref<uint16> list(wireNeighbours[n] + offset + i * Grid::cellCapacity, Grid::cellCapacity);
       for(size_t j=0; list[j] && j<Grid::cellCapacity; j++) {
        assert_(grainWireCount < grainWireA.capacity, grainWireCount, grainWireA.capacity);
        size_t B = list[j]-1;
        //found.append(a);
        size_t index = grainWireCount;
        grainWireA[index] = a; // Grain
        grainWireB[index] = B; // Wire
        for(size_t i: range(grainWire)) {
         size_t b = grainWireIndices[a*grainWire+i][0]; // wire index + 1
         if(!b) break;
         b--;
         if(b == B) { // Repack existing friction
          size_t j = grainWireIndices[a*grainWire+i][1];
          grainWireLocalAx[index] = this->grainWireLocalAx[j];
          grainWireLocalAy[index] = this->grainWireLocalAy[j];
          grainWireLocalAz[index] = this->grainWireLocalAz[j];
          grainWireLocalBx[index] = this->grainWireLocalBx[j];
          grainWireLocalBy[index] = this->grainWireLocalBy[j];
          grainWireLocalBz[index] = this->grainWireLocalBz[j];
          goto break__;
         }
        } /*else*/ {
         grainWireLocalAx[index] = 0; grainWireLocalAy[index] = 0; grainWireLocalAz[index] = 0;
         grainWireLocalBx[index] = 0; grainWireLocalBy[index] = 0; grainWireLocalBz[index] = 0;
        }
        break__:;
        grainWireCount++;
       }
      }
     }
#endif
     /*for(size_t a: range(grain.count)) {
      float Ax = grain.Px[a], Ay = grain.Py[a], Az = grain.Pz[a];
      float Bx = wire.Px[B], By = wire.Py[B], Bz = wire.Pz[B];
      float Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
      float length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
      float depth = Grain::radius + Wire::radius - length;
      if(depth > 0) assert_(found.contains(a), found, a, Ax,Ay,Az, Bx,By,Bz);
      //else assert_(!found.contains(b));
     }*/
    }

    {//Locker lock(this->lock);
     //grainWireCount2 = ::min(grainWireCount2, grainWireCount);
     this->grainWireLocalAx = move(grainWireLocalAx);
     this->grainWireLocalAy = move(grainWireLocalAy);
     this->grainWireLocalAz = move(grainWireLocalAz);
     this->grainWireLocalBx = move(grainWireLocalBx);
     this->grainWireLocalBy = move(grainWireLocalBy);
     this->grainWireLocalBz = move(grainWireLocalBz);
     //grainWireCount2 = grainWireCount;
    }
   }
   /*//assert(minD3 < 2*Grain::radius/sqrt(3.) - Grain::radius, minD3);
   grainSideGlobalMinD2 = minD3 - Grain::radius;
   assert(grainSideGlobalMinD2 > 0, grainSideGlobalMinD2);*/
   // Aligns
   size_t gWC = grainWireCount;
   while(gWC%8) { // Content does not matter as long as intersection evaluation does not trigger exceptions
    grainWireA[gWC] = 0;
    grainWireB[gWC] = 0;
    gWC++;
   }
   assert(gWC <= grainWireA.capacity);

   {
    // Evaluates (packed) intersections from (packed) (TODO: verlet) lists
    size_t grainWireContactCount = 0;
    buffer<int> grainWireContact(grainWireCount);
    grainWireIntersectTime.start();
    for(size_t index = 0; index < grainWireCount; index += 8) {
     v8si A = *(v8si*)(grainWireA.data+index), B = *(v8si*)(grainWireB.data+index);
     v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
     v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
     v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
     v8sf depth = Grain::radius8 + Wire::radius8 - length;
     for(size_t k: range(8)) {
      if(index+k == grainWireCount) break /*2*/;
      if(depth[k] > 0) {
       static float depthMax = 0.01;
       /*if(B[k] == 266 || (A[k] == 110 || A[k] == 113)) log(timeStep, B[k], A[k], depth[k], Wire::radius/depth[k], Ax[k],Ay[k],Az[k], Bx[k],By[k],Bz[k], Rx[k],Ry[k],Rz[k], length[k],
                                          grain.Vx[A[k]], grain.Vy[A[k]], grain.Vz[A[k]],
                                          wire.Vx[B[k]], wire.Vy[B[k]], wire.Vz[B[k]]);*/
       if(depth[k] > depthMax) {
        log("Max overlap", B[k], A[k], depth[k], Wire::radius/depth[k]);
        depthMax=depth[k];
        if(depth[k] >  0.02) {
         highlightWire.append(B[k]);
         highlightGrain.append(A[k]);
         processState = ProcessState::Fail;
         return false;
        }
       }
       grainWireContact[grainWireContactCount] = index+k;
       grainWireContactCount++;
      } else {
       // DEBUG:
       grainWireLocalAx[index+k] = 0; grainWireLocalAy[index+k] = 0; grainWireLocalAz[index+k] = 0;
       grainWireLocalBx[index+k] = 0; grainWireLocalBy[index+k] = 0; grainWireLocalBz[index+k] = 0;
      }
     }
    }
    grainWireIntersectTime.stop();
#else
   {
    size_t grainWireContactCount = 0;
    buffer<int> grainWireContact(grainCount8 * grainWire);
    grainWireCount = 0;
    for(size_t b: range(wire.count)) {
     for(size_t a: range(grain.count)) {
      float Ax = grain.Px[a], Ay = grain.Py[a], Az = grain.Pz[a];
      float Bx = wire.Px[b], By = wire.Py[b], Bz = wire.Pz[b];
      float Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
      float length = sqrt(Rx*Rx + Ry*Ry + Rz*Rz);
      float depth = Grain::radius + Wire::radius - length;
      if(depth > 0) {
       assert_(grainWireCount < grainWireA.capacity, "B", grainWireCount, grainWireA.capacity);
       size_t index = grainWireCount;
       grainWireA[index] = a;
       grainWireB[index] = b;
       grainWireCount++;
       assert_(grainWireContactCount < grainWireContact.capacity);
       grainWireContact[grainWireContactCount] = index;
       grainWireContactCount++;
      }
     }
    }
#endif

    // Aligns with invalid contacts
    size_t gWCC = grainWireContactCount;
    while(gWCC%8)
     grainWireContact[gWCC++] = grainWireCount; // Invalid entry

    // Evaluates forces from (packed) intersections
    buffer<float> Fx(gWCC), Fy(gWCC), Fz(gWCC);
    buffer<float> TAx(gWCC), TAy(gWCC), TAz(gWCC);
    grainWireForceTime.start();
    for(size_t i = 0; i < grainWireContactCount; i += 8) {
     v8si contacts = *(v8si*)(grainWireContact.data+i);
     v8si A = gather(grainWireA, contacts), B = gather(grainWireB, contacts);
     // FIXME: Recomputing from intersection (more efficient than storing?)
     v8sf Ax = gather(grain.Px, A), Ay = gather(grain.Py, A), Az = gather(grain.Pz, A);
     v8sf Bx = gather(wire.Px.data, B), By = gather(wire.Py.data, B), Bz = gather(wire.Pz.data, B);
     v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
     v8sf length = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
     v8sf depth = Grain::radius8 + Wire::radius8 - length;
     v8sf Nx = Rx/length, Ny = Ry/length, Nz = Rz/length;
     /*contact(grain, A, wire, B, depth, -Rx, -Ry, -Rz, Nx, Ny, Nz,
             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);*/
     v8sf RAx = - Grain::radius8 * Nx, RAy = - Grain::radius8 * Ny, RAz = - Grain::radius8 * Nz;
#define FRICTION 1
#if FRICTION
     // Gather static frictions
     v8sf localAx = gather(grainWireLocalAx, contacts);
     v8sf localAy = gather(grainWireLocalAy, contacts);
     v8sf localAz = gather(grainWireLocalAz, contacts);
     v8sf localBx = gather(grainWireLocalBx, contacts);
     v8sf localBy = gather(grainWireLocalBy, contacts);
     v8sf localBz = gather(grainWireLocalBz, contacts);
#else
     v8sf localAx = _0f, localAy = _0f, localAz = _0f;
     v8sf localBx = _0f, localBy = _0f, localBz = _0f;
#endif
     contact<Grain,Wire>(grain, A, wire, B, depth,
                          RAx, RAy, RAz,
                          Nx, Ny, Nz,
                          Ax, Ay, Az,
                          Bx, By, Bz,
                          localAx, localAy, localAz,
                          localBx, localBy, localBz,
                          *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i],
                          *(v8sf*)&TAx[i], *(v8sf*)&TAy[i], *(v8sf*)&TAz[i]
                          );
#if FRICTION
     // Scatter static frictions
     scatter(grainWireLocalAx, contacts, localAx);
     scatter(grainWireLocalAy, contacts, localAy);
     scatter(grainWireLocalAz, contacts, localAz);
     scatter(grainWireLocalBx, contacts, localBx);
     scatter(grainWireLocalBy, contacts, localBy);
     scatter(grainWireLocalBz, contacts, localBz);
#endif
    }

    grainWireAddTime.start();
    for(size_t i = 0; i < grainWireContactCount; i++) { // Scalar scatter add
     size_t index = grainWireContact[i];
     size_t a = grainWireA[index];
     size_t b = grainWireB[index];
     grain.Fx[a] += Fx[i];
     wire.Fx[b] -= Fx[i];
     grain.Fy[a] += Fy[i];
     wire.Fy[b] -= Fy[i];
     grain.Fz[a] += Fz[i];
     wire.Fz[b] -= Fz[i];
     grain.Tx[a] += TAx[i];
     grain.Ty[a] += TAy[i];
     grain.Tz[a] += TAz[i];
     highlightWire.append(b);
    }
    grainWireAddTime.stop();
   } // GrainWireContact

   {// Bottom
    size_t gBC = wireBottomCount;
    while(gBC%8) wireBottom[gBC++] = wire.count;
    assert(gBC <= wireCount8, gBC);
    v8sf Bz_R = float8(plate.Pz[0]+Wire::radius);
    buffer<float> Fx(gBC), Fy(gBC), Fz(gBC);
    for(size_t i = 0; i < gBC; i += 8) {
     v8si A = *(v8si*)&wireBottom[i];
     v8sf depth = Bz_R - gather(wire.Pz, A);
     contact(wire, A, plate, _0i, depth, _0f, _0f, _1f,
             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
    }
    for(size_t i = 0; i < wireBottomCount; i++) { // Scalar scatter add
     size_t a = wireBottom[i];
     assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
     wire.Fx[a] += Fx[i];
     wire.Fy[a] += Fy[i];
     wire.Fz[a] += Fz[i];
     plate.Fx[0] -= Fx[i];
     plate.Fy[0] -= Fy[i];
     plate.Fz[0] -= Fz[i];
    }
   }

   {// Top
    size_t gTC = wireTopCount;
    while(gTC%8) wireTop[gTC++] = wire.count;
    v8sf Bz_R = float8(plate.Pz[1]-Wire::radius);
    buffer<float> Fx(gTC), Fy(gTC), Fz(gTC);
    for(size_t i = 0; i < gTC; i += 8) {
     v8si A = *(v8si*)&wireTop[i];
     v8sf depth = gather(wire.Pz, A) - Bz_R;
     contact(wire, A, plate, _1i, depth, _0f, _0f, -_1f,
             *(v8sf*)&Fx[i], *(v8sf*)&Fy[i], *(v8sf*)&Fz[i]);
    }
    for(size_t i = 0; i < wireTopCount; i++) { // Scalar scatter add
     size_t a = wireTop[i];
     assert(isNumber(Fx[i]) && isNumber(Fy[i]) && isNumber(Fz[i]));
     wire.Fx[a] += Fx[i];
     wire.Fy[a] += Fy[i];
     wire.Fz[a] += Fz[i];
     plate.Fx[1] -= Fx[i];
     plate.Fy[1] -= Fy[i];
     plate.Fz[1] -= Fz[i];
    }
   }

   // Tension
   for(size_t i: range(1, wire.count)) { // TODO: SIMD
    //vec4f fT = tension(wire, i-1, i);
    size_t a = i-1, b = i;
    vec3 relativePosition = wire.position(a) - wire.position(b);
    vec3 length = ::length(relativePosition));
    vec3 x = length - wire.internodeLength;
    vec3 fS = - wire.tensionStiffness * x;
    vec3 direction = relativePosition/length;
    vec3 relativeVelocity = wire.velocity(a) - A.velocity(b);
    vec3 fB = - wire.tensionDamping * dot(direction, relativeVelocity);
    vec3 fT = (fS + fB) * direction;
    wire.Fx[a] += fT.x;
    wire.Fy[a] += fT.y;
    wire.Fz[a] += fT.z;
    wire.Fx[b] -= fT.x;
    wire.Fy[b] -= fT.y;
    wire.Fz[b] -= fT.z;
   }
  }

  // Integration

  // Grain
  float maxGrainV = 0;
  grainIntegrationTime.start();
  for(size_t i: range(grain.count)) { // TODO: SIMD
   System::step(grain, i);
   maxGrainV = ::max(maxGrainV, length(grain.velocity(i)));
  }
  grainIntegrationTime.stop();
  float maxGrainGrainV = maxGrainV + maxGrainV;
  grainGrainGlobalMinD6 -= maxGrainGrainV * dt;

  // Wire
  wireIntegrationTime.start();
  for(size_t i: range(wire.count)) { // TODO: SIMD
   System::step(wire, i);
  }

  // All around pressure
  if(processState == ProcessState::Pack) {
   plate.Fz[0] += alpha * pressure * PI * sq(side.radius);
   bottomForce = plate.Fz[0];
   System::step(plate, 0);
   plate.Vz[0] = ::max(0.f, plate.Vz[0]); // Only compression
   plate.Vz[0] /= 2;

   plate.Fz[1] -= alpha * pressure * PI * sq(side.radius);
   topForce = plate.Fz[1];
   System::step(plate, 1);
   plate.Vz[1] = ::min(plate.Vz[1], 0.f); // Only compression
   plate.Vz[1] /= 2;
  } else {
   bottomForce = plate.Fz[0];
   topForce = plate.Fz[1];
  }

  if(size_t((1./60)/dt)==0 || timeStep%size_t((1./60)/dt) == 0) {
   if(processState==ProcessState::Load) { // Records results
    /*assert(grainSideCount);
    float radius = grainSideRadiusSum/grainSideCount + Grain::radius;
    assert(isNumber(radius));*/
    float radius = side.radius;
    //float radialForceSum = sideForce; //pressure * 2 * PI * radius * height;
    float plateForce = plate.Fz[1] - plate.Fz[0];
    //if(sideForce < 0) log(sideForce);
    stream.write(str(radius, height, plateForce, -sideForce/*, grainKineticEnergy, grainSideCount*/)+'\n');

    // Snapshots every 1% strain
    /*float strain = (1-height/(topZ0-bottomZ0))*100;
    if(round(strain) > round(lastSnapshotStrain)) {
     lastSnapshotStrain = strain;
     snapshot(str(round(strain))+"%");
    }*/
   }
  }

  if(processState==Pour && pattern /*&& currentPourHeight-Grain::radius-Wire::radius > 0*/) { // Generates wire (winch)
   vec2 end;
   if(pattern == Helix) { // Simple helix
    float a = winchAngle;
    float r = patternRadius;
    end = vec2(r*cos(a), r*sin(a));
    winchAngle += winchRate * dt;
   }
   else if(pattern == Cross) { // Cross reinforced helix
    if(currentRadius < -patternRadius) { // Radial -> Tangential (Phase reset)
     currentRadius = patternRadius;
     lastAngle = winchAngle+PI;
     winchAngle = lastAngle;
    }
    if(winchAngle < lastAngle+loopAngle) { // Tangential phase
     float a = winchAngle;
     float r = patternRadius;
     end = vec2(r*cos(a), r*sin(a));
     winchAngle += winchRate * dt;
    } else { // Radial phase
     float a = winchAngle;
     float r = currentRadius;
     end = vec2(r*cos(a), r*sin(a));
     currentRadius -= winchRate * dt * patternRadius; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    float R = patternRadius, r = R * loopAngle / (2*PI);
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += winchRate*R/r * dt;
   } else error("Unknown pattern:", int(pattern));
   float z = currentPourHeight+Grain::radius+Wire::radius; // Over pour
   //float z = currentPourHeight-Grain::radius-Wire::radius; // Under pour
   vec4f relativePosition = (v4sf){end[0], end[1], z, 0} - (v4sf){wire.Px[wire.count-1], wire.Py[wire.count-1], wire.Pz[wire.count-1], 0};
  vec4f length = sqrt(sq3(relativePosition));
  if(length[0] >= Wire::internodeLength) {
   //Locker lock(this->lock);
   assert(wire.count < wire.capacity, wire.capacity);
   v4sf p = (v4sf){wire.Px[wire.count-1], wire.Py[wire.count-1], wire.Pz[wire.count-1], 0} + float3(Wire::internodeLength) * relativePosition/length;
   if(p[2] >= plate.Pz[1]) {
    skip = false;
    processState = Pack;
    log("Pour->Pack", grain.count, wire.count,  grainKineticEnergy / grain.count*1e6, voidRatio);
    packStart = timeStep;
   } else {
    size_t i = wire.count++;
    wire.Px[i] = p[0];
    wire.Py[i] = p[1];
    wire.Pz[i] = p[2];
    /*min[0] = ::min(min[0], wire.Px[i]);
   min[1] = ::min(min[1], wire.Py[i]);
   min[2] = ::min(min[2], wire.Pz[i]);
   max[0] = ::max(max[0], wire.Px[i]);
   max[1] = ::max(max[1], wire.Py[i]);
   max[2] = ::max(max[2], wire.Pz[i]);*/
    wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
#if GEAR
    for(size_t n: range(3)) {
     wire.PDx[n][i] = 0;
     wire.PDy[n][i] = 0;
     wire.PDz[n][i] = 0;
    }
#endif
    //wire.frictions.set(i);
   }
  }
 }
 wireIntegrationTime.stop();
 stepTime.stop();
 partTime.stop();
 timeStep++;
 return true;
}

String info() {
 array<char> s;
 s.append(processStates[processState]);
 s.append(" R="_+str(side.radius));
 s.append(" "_+str(timeStep*dt));
 if(processState>=ProcessState::Load) {
  float height = plate.Pz[1]-plate.Pz[0];
  s.append(" "_+str(100*(1-height/(topZ0-bottomZ0))));
 }
 /*s.append(" "_+str(pressure,0u,1u));
 s.append(" "_+str(dt,0u,1u));
 s.append(" "_+str(grain.count));
 s.append(" "_+str(int(timeStep*this->dt))+"s"_);*/
 if(grain.count) s.append(" "_+decimalPrefix(grainKineticEnergy/grain.count, "J"));
 /*if(processState>=ProcessState::Load) {
  float bottomZ = plate.Pz[0], topZ = plate.Pz[1];
  float displacement = (topZ0-topZ+bottomZ-bottomZ0);
  s.append(" "_+str(displacement/(topZ0-bottomZ0)*100));
 }
 if(grain.count) {
  float height = plate.Pz[1] - plate.Pz[0];
  float voidRatio = PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1;
  s.append(" Ratio:"+str(int(voidRatio*100))+"%");
 }
 if(wire.count) {
  float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
  s.append(" Wire density:"+str(int(wireDensity*100))+"%");
 }
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 if(processState >= ProcessState::Pack) {
  s.append(" "+str(round(lastBalanceAverage), round(balanceAverage)));
  float plateForce = topForce - bottomForce;
  float stress = plateForce/(2*PI*sq(side.initialRadius));
  s.append(" "_+str(decimalPrefix(stress, "Pa"), decimalPrefix(pressure, "Pa"), decimalPrefix(sideForce, "Pa")));
 }*/
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 return move(s);
}
};

