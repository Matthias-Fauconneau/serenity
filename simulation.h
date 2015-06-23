#include "system.h"

struct Lattice {
 const v4sf scale;
 const v4sf min, max;
 const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
 buffer<uint16> indices {size_t(size.z*size.y*size.x)};
 const v4sf XYZ0 {float(1), float(size.x), float(size.y*size.x), 0};
 Lattice(float scale, v4sf min, v4sf max) :
   scale(float3(scale)),
   min(min - float3(2./scale)),
   max(max + float3(2./scale)) { // Avoids bound check
  indices.clear(0);
 }
 inline size_t index(v4sf p) {
  //assert(mask(p<min && p>=max)==0, min, p, max);
  return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0];
 }
 inline uint16& operator()(v4sf p) {
  //assert(index(p) < indices.size);
  return indices[index(p)];
 }
};

struct Simulation : System {
 v4sf min = _0f, max = _0f;
 /*Lattice wireLattice{float(sqrt(3.)/(2*Wire::radius)),
    v4sf{-16*Grain::radius,-16*Grain::radius,    -Grain::radius,0},
    v4sf{ 16*Grain::radius, 16*Grain::radius,15*Grain::radius,0}};*/

 // Process
 const float pourRadius = side.castRadius - Grain::radius - Wire::radius;
 const float winchRadius = side.castRadius - Grain::radius - Wire::radius;
   /*- (loopAngle?Grain::radius:0)*/;
   //- (loopAngle&&0?Wire::radius:Grain::radius);
 const float initialTension = 0; // -2
 float pourHeight = Floor::height+Grain::radius;
 float winchAngle = 0;

 float kT=0, kR=0;

 Random random;
 enum { Pour, Load, Release, Done } processState = Pour;
 int direction = 1, directionSwitchCount = 0;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
 Time wireLatticeTime, wireContactTime, wireIntegrationTime;

 Lock lock;
 HList<Plot> plots;

 Simulation() {
  // Initial wire node
  size_t i = wire.count++;
  wire.position[i] = vec3(winchRadius,0,pourHeight);
  min = ::min(min, wire.position[i]);
  max = ::max(max, wire.position[i]);
  wire.velocity[i] = _0f;
  for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
  wire.frictions.set(i);
 }

 inline v4sf tension(size_t a, size_t b) {
  v4sf relativePosition = wire.position[a] - wire.position[b];
  v4sf length = sqrt(sq3(relativePosition));
  v4sf x = length - Wire::internodeLength4;
  tensionEnergy += 1./2 * Wire::tensionStiffness[0] * sq3(x)[0];
  v4sf fS = - Wire::tensionStiffness * x;
  v4sf direction = relativePosition/length;
  v4sf relativeVelocity = wire.velocity[a] - wire.velocity[b];
  v4sf fB = - wire.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 void step() {
  stepTime.start();
  // Process
  //Locker lock(this->lock);
  /*if(processState == Pour && pourHeight >= size.z/scale[0]) {
   log("Max height");
   processState = Load;
  }*/
  if(processState == Pour) {
   if(pourHeight>=2 || grain.count == grain.capacity
               || (wire.capacity && wire.count == wire.capacity)) {
    log("Poured", "Grain", grain.count, "Wire", wire.count);
    processState=Load;
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && pourHeight >= Grain::radius) for(;;) {
     v4sf p {random()*2-1,random()*2-1, pourHeight, 0};
     if(length2(p)>1) continue;
     v4sf newPosition {pourRadius*p[0], pourRadius*p[1], p[2], 0}; // Within cylinder
     // Finds lowest Z (reduce fall/impact energy)
     //float maxZ = newPosition[2];
     newPosition[2] = Grain::radius;
     for(auto p: grain.position.slice(0, grain.count)) {
      float x = length(toVec3(p - newPosition).xy());
      if(x < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius) - sq(x));
       newPosition[2] = ::max(newPosition[2], p[2]+dz);
      }
     }
     //assert_(newPosition[2] <= maxZ);
     if(newPosition[2] > pourHeight) break;
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     /*for(auto p: grain.position.slice(0, grain.count)) {
      if(length(p - newPosition) < Grain::radius+Grain::radius)
       goto break2_;
     }*/
     for(auto p: grain.position.slice(0, grain.count)) {
      if(::length(p - newPosition) < Grain::radius+Grain::radius-0x1p-24)
       error(newPosition, p, log2(abs(::length(p - newPosition) -  2*Grain::radius)));
     }
     Locker lock(this->lock);
     size_t i = grain.count++;
     grain.position[i] = newPosition;
     min = ::min(min, grain.position[i]);
     max = ::max(max, grain.position[i]);
     grain.velocity[i] = _0f;
     for(size_t n: range(3)) grain.positionDerivatives[n][i] = _0f;
     grain.angularVelocity[i] = _0f;
     for(size_t n: range(3)) grain.angularDerivatives[n][i] = _0f;
     grain.frictions.set(i);
     float t0 = 2*PI*random();
     float t1 = acos(1-2*random());
     float t2 = (PI*random()+acos(random()))/2;
     grain.rotation[i] = {sin(t0)*sin(t1)*sin(t2),
                                    cos(t0)*sin(t1)*sin(t2),
                                    cos(t1)*sin(t2),
                                    cos(t2)};
     break;
    }
break2_:;
    pourHeight += winchSpeed * dt;
   }
  }
  if(processState == Load && !load.count) {
   float maxZ = 0, vz=0;
   for(size_t i: range(grain.count)) {
    auto p = grain.position[i];
    if(p[2]>maxZ) {
     maxZ = p[2];
     vz = grain.velocity[i][2];
    }
   }
   if(vz > -1) {
    log(vz);
    Locker lock(this->lock);
    load.count++;
    load.position[0] = vec3(0,0,maxZ+Grain::radius+Grain::radius);
    load.velocity[0] = _0f;
    for(size_t n: range(3)) load.positionDerivatives[n][0] = _0f;
   }
  }
  if(processState==Release) {
   if(side.castRadius < 15*Grain::radius) {
    if(!releaseTime) releaseTime = timeStep*dt;
    //side.castRadius = side.initialRadius * exp(sq((timeStep*dt-releaseTime)/(16*T)));
    //side.castRadius = side.initialRadius * exp((timeStep*dt-releaseTime)/(1*T));
    side.castRadius = 15*Grain::radius;
    //side.castRadius = ::min(side.castRadius, 15*Grain::radius);*/
    // + Pressure
    /*float h = load.count ? load.position[0][2] : pourHeight;
    float P = side.force.radialSum / (2*PI*side.castRadius*h);
    static float v = 0;
    v += dt * (0x1p-5 * L/T) * P / (M*L/T/T/(L*L));
    side.castRadius += dt * v;*/
   } else {
    log("Released");
    processState = Done;
   }
  }

  // Initialization
  gravityEnergy = 0, contactEnergy = 0, tensionEnergy = 0, tensionEnergy = 0;
  side.force.radialSum = 0;

  // Load
  if(load.count) load.force[0] = float4(load.mass) * g;

  // Grain
  grainTime.start();

  Lattice grainLattice(sqrt(3.)/(2*Grain::radius), min, max);

  parallel_chunk(grain.count, [this,&grainLattice](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    grain.force[a] = float4(grain.mass) * g;
    grain.torque[a] = _0f;
    penalty(grain, a, floor, 0);
    if(load.count) {
     penalty(grain, a, load, 0);
     //penalty(grain, a, lid, 0);
    }
    if(processState != Done) penalty(grain, a, side, 0);
    grainLattice(grain.position[a]) = 1+a;
   }
  }, 1);
  grainInitializationTime.stop();

  grainContactTime.start();
  parallel_chunk(grainLattice.indices.size,
                 [this,&grainLattice](uint, size_t start, size_t size) {
   const int Y = grainLattice.size.y, X = grainLattice.size.x;
   const uint16* chunk = grainLattice.indices.begin() + start;
   assert(start==0);
   for(size_t i: range(size)) {
    const uint16* current = chunk + i;
    size_t a = *current;
    if(!a) continue;
    a--;
    for(int z: range(2+1)) for(int y: range(-2, 2+1)) for(int x: range(-2, 2+1)) {
     int di = z*Y*X + y*X + x;
     if(di <= 0) continue; // FIXME: unroll
     const uint16* neighbour = current + z*Y*X + y*X + x;
     size_t b = *neighbour;
     if(!b) continue;
     b--;
     penalty(grain, a, grain, b);
    }
   }
  }, 1);
  grainContactTime.stop();
  grainTime.stop();

  // Wire
  wireTime.start();

  /*if(mask(min < wireLattice.min || max > wireLattice.max))
   log(min, wireLattice.min, wireLattice.max, max);*/

  /*wireLatticeTime.start();
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    size_t index = wireLattice.index(wire.position[a]);
    if(!(index < wireLattice.indices.size)) { log(wire.position[a], max); continue; }
    wireLattice.indices[index] = 1+a;
    //wireLattice(wire.position[a]) = 1+a;
   }
  }, 1);
  wireLatticeTime.stop();*/
  //log(wireLattice.indices.size*2/1024); // 2M

  wireContactTime.start();
  if(wire.count) parallel_chunk(wire.count,
                                [this,&grainLattice](uint, size_t start, size_t size) {
   uint16* grainBase = grainLattice.indices.begin();
   const int gX = grainLattice.size.x, gY = grainLattice.size.y;
   const uint16* grainNeighbours[3*3] = {
    grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
    grainBase-gX-1, grainBase-1, grainBase+gX-1,
    grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
   };

   /*uint16* wireBase = wireLattice.indices.begin();
   const int wX = wireLattice.size.x, wY = wireLattice.size.y;*/
   /*const uint16* wireNeighbours[3*3] = {
    wireBase-wX*wY-wX-1, wireBase-wX*wY-1, wireBase-wX*wY+wX-1,
    wireBase-wX-1, wireBase-1, wireBase+wX-1,
    wireBase+wX*wY-wX-1, wireBase+wX*wY-1, wireBase+wX*wY+wX-1
   };*/
   /*const int r = int( sqrt(sq(2*Wire::radius)+sq(Wire::internodeLength/2))
                     * wireLattice.scale[0]) + 1;*/
   /*const int r =  1;
   const int n = r+1+r, N = n*n;
   static unused bool once = ({ log(r,n,N); true; });
   const uint16* wireNeighbours[N];
   for(int z: range(n)) for(int y: range(n))
    wireNeighbours[z*n+y] = wireBase+(z-r)*wY*wX+(y-r)*wX-r;*/

   wire.force[start] = float4(wire.mass) * g;
   { // First iteration
    size_t i = start;
    wire.force[i+1] = float4(wire.mass) * g;
    if(i > 0) {
     if(start > 1 && wireBendStiffness) {// Previous torsion spring on first
      size_t i = start-1;
      v4sf A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      v4sf a = C-B, b = B-A;
      v4sf c = cross(a, b);
      v4sf l = sqrt(sq3(c));
      if(l[0]) {
       float p = wireBendStiffness * atan(l[0], dot3(a, b)[0]);
       v4sf dap = cross(a, cross(a,b)) / (sq3(a) * l);
       wire.force[i+1] += float3(p) * (-dap);
      }
     }
     // First tension
     v4sf fT = tension(i-1, i);
     wire.force[i] -= fT;
     if(wireBendStiffness) { // First torsion
      v4sf A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      v4sf a = C-B, b = B-A;
      v4sf c = cross(a, b);
      v4sf l = sqrt(sq3(c));
      if(l[0]) {
       float p = wireBendStiffness * atan(l[0], dot3(a, b)[0]);
       v4sf dap = cross(a, cross(a,b)) / (sq3(a) * l);
       v4sf dbp = cross(b, cross(b,a)) / (sq3(b) * l);
       wire.force[i+1] += float3(p) * (-dap);
       wire.force[i] += float3(p) * (dap - dbp);
       //wire.force[i-1] += float3(p) * (dbp); //-> "Tension to first node of next chunk"
       if(1) {
        v4sf A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
        v4sf axis = cross(C-B, B-A);
        v4sf l = sqrt(sq3(axis));
        if(l[0]) {
         float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
         wire.force[i] += float3(wireBendDamping * angularVelocity / 2)
           * cross(axis/l, C-A);
        }
       }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    /*// Wire - Wire
    {size_t offset = wireLattice.index(wire.position[i]);
     if(!(offset < wireLattice.indices.size)) { log(wire.position[i], max); }
     else
    for(size_t n: range(N)) {
#if 0
     v4hi line = loada(wireNeighbours[n] + offset);
     if(line[0] > i+2) penalty(wire, i, wire, line[0]-1);
     if(line[1] > i+2) penalty(wire, i, wire, line[1]-1);
     if(line[2] > i+2) penalty(wire, i, wire, line[2]-1);
#else
     for(size_t x: range(n)) {
      uint16 index = wireNeighbours[n][offset+x];
      if(index > i+2) penalty(wire, i, wire, index-1);
     }
#endif
    }}*/
   }
   for(size_t i: range(start+1, start+size-1)) {
    // Gravity
    wire.force[i+1] = float4(wire.mass) * g;
    // Tension
    v4sf fT = tension(i-1, i);
    wire.force[i-1] += fT;
    wire.force[i] -= fT;
    if(wireBendStiffness) {// Torsion springs (Bending resistance)
     v4sf A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     v4sf a = C-B, b = B-A;
     v4sf c = cross(a, b);
     v4sf length4 = sqrt(sq3(c));
     float length = length4[0];
     if(length) {
      v4sf p = float3(wireBendStiffness * atan(length, dot3(a, b)[0]));
      v4sf dap = cross(a, cross(a,b)) / (sq3(a) * length4);
      v4sf dbp = cross(b, cross(b,a)) / (sq3(b) * length4);
      wire.force[i+1] += p* (-dap);
      wire.force[i] += p * (dap - dbp);
      wire.force[i-1] += p * (dbp);
      if(1) {
       v4sf A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
       v4sf axis = cross(C-B, B-A);
       v4sf length4 = sqrt(sq3(axis));
       float length = length4[0];
       if(length) {
        float angularVelocity = atan(length, dot3(C-B, B-A)[0]);
        wire.force[i] += float3(wireBendDamping * angularVelocity / 2)
          * cross(axis/length4, C-A);
       }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    /*// Wire - Wire
    {size_t offset = wireLattice.index(wire.position[i]);
     if(!(offset < wireLattice.indices.size)) { log(wire.position[i], max); continue; }
    for(size_t n: range(N)) {
#if 0
     v4hi line = loada(wireNeighbours[n] + offset);
     if(line[0] > i+2) penalty(wire, i, wire, line[0]-1);
     if(line[1] > i+2) penalty(wire, i, wire, line[1]-1);
     if(line[2] > i+2) penalty(wire, i, wire, line[2]-1);
#else
     for(size_t x: range(n)) {
      uint16 index = wireNeighbours[n][offset+x];
      if(index > i+2) penalty(wire, i, wire, index-1);
     }
#endif
    }}*/
   }

   // Last iteration
   if(size>1) {
    size_t i = start+size-1;
    // Gravity
    wire.force[i] = float4(wire.mass) * g;
    // Tension
    v4sf fT = tension(i-1, i);
    wire.force[i-1] += fT;
    wire.force[i] -= fT;
    // Torsion with next chunk
    if(i+1 < wire.count) {
     v4sf A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     v4sf a = C-B, b = B-A;
     v4sf c = cross(a, b);
     v4sf l = sqrt(sq3(c));
     if(l[0]) {
      float angle = atan(l[0], dot3(a, b)[0]);
      torsionEnergy += 1./2 * wireBendStiffness * L * sq(angle);
      v4sf dap = cross(a, cross(a,b)) / (sq3(a) * l);
      v4sf dbp = cross(b, cross(b,a)) / (sq3(b) * l);
      //wire.force[i+1] += float3(p) * (-dap); //-> "Previous torsion spring on first"
      float p = wireBendStiffness * angle;
      wire.force[i] += float3(p) * (dap - dbp);
      wire.force[i-1] += float3(p) * (dbp);
      if(1) {
       v4sf A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
       v4sf axis = cross(C-B, B-A);
       v4sf l = sqrt(sq3(axis));
       if(l[0]) {
        float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
        wire.force[i] += float3(wireBendDamping * angularVelocity / 2)
          * cross(axis/l, C-A);
       }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    /*// Wire - Wire
    {size_t offset = wireLattice.index(wire.position[i]);
     if(!(offset < wireLattice.indices.size)) { log(wire.position[i], max); }
      else
    for(size_t n: range(N)) {
#if 0
     v4hi line = loada(wireNeighbours[n] + offset);
     if(line[0] > i+2) penalty(wire, i, wire, line[0]-1);
     if(line[1] > i+2) penalty(wire, i, wire, line[1]-1);
     if(line[2] > i+2) penalty(wire, i, wire, line[2]-1);
#else
     for(size_t x: range(n)) {
      uint16 index = wireNeighbours[n][offset+x];
      if(index > i+2) penalty(wire, i, wire, index-1);
     }
#endif
    }}*/
   }

   { // Tension to first node of next chunk
    size_t i = start+size;
    if(i < wire.count) {
     wire.force[i-1] += tension(i-1, i);
      // Torsion to first node of next chunk
     if(i+1<wire.count && wireBendStiffness) {
      v4sf A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      v4sf a = C-B, b = B-A;
      v4sf c = cross(a, b);
      v4sf l = sqrt(sq3(c));
      if(l[0]) {
       float p = wireBendStiffness * atan(l[0], dot3(a, b)[0]);
       v4sf dbp = cross(b, cross(b,a)) / (sq3(b) * l);
       wire.force[i-1] += float3(p) * (dbp);
      }
     }
    }
   }
  }, 1/*::threadCount*/);
  wireContactTime.stop();

  /*wireContactTime.start();
  // TODO: lattice split parallelization
  parallel_chunk(wire.count, [&](uint, size_t start, size_t size) {
   uint16* base = indices.begin();
   const int X = grainLattice.size.x, Y = grainLattice.size.y;
   const v4sf XYZ0 {1, float(X), float(Y*X), 0};
   const uint16* neighbours[3*3] = {
    base-X*Y-X-1, base-X*Y-1, base-X*Y+X-1,
    base-X-1, base-1, base+X-1,
    base+X*Y-X-1, base+X*Y-1, base+X*Y+X-1
   };
   for(size_t i: range(start, start+size)) {
    // Bounds
    penalty(wire, i, floor, 0);
    // Wire - Grain
    size_t offset = dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(wire.position[i]-min))))[0];
    for(size_t n: range(3*3)) {
     v4hi line = loada(neighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }
   }
  },1, ::threadCount);
  wireContactTime.stop();*/

  /*wireLatticeTime.start();
  // Clears lattice
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    size_t index = wireLattice.index(wire.position[a]);
    if(!(index < wireLattice.indices.size)) { log(wire.position[a], max); continue; }
    wireLattice.indices[index] = 0;
   }
  }, 1);
  wireLatticeTime.stop();*/

  // Integration
  min = _0f; max = _0f;

  float replacementLength =
    Wire::internodeLength*(1+initialTension/Wire::tensionStiffness[0]);
  v4sf length;
  v4sf direction;
  if(processState==Pour) {
   //float r = (mod(winchAngle / loopAngle, 2) < 1 ? 1 : -1) * winchRadius;
   float a = winchAngle, r = 1, dz = 0;
   if(loopAngle) {
#if 0
     float t = mod(winchAngle / loopAngle, 4);
     /**/  if(/*0 <*/ t < 1) r = 1;
     else if(/*1 <*/ t < 2) r = 1-2*(t-1);
     else if(/*2 <*/ t < 3) r = -1;
     else    /*3 <    t < 4*/r = -1+2*(t-3);
#elif 1
    float t = mod(winchAngle / loopAngle, 4);
    float b = (winchAngle - t*loopAngle)/2;
    /**/  if(/*0 <*/ t < 1) r = 1, a=b+t*loopAngle;
    else if(/*1 <*/ t < 2) r = 1-2*(t-1), a=b+loopAngle;
    else if(/*2 <*/ t < 3) r = -1, a=b+loopAngle+/*-*/(t-2)*loopAngle;
    else    /*3 <    t < 4*/r = -1+2*(t-3), a=b+2*loopAngle;
#endif
   }
   dz = Grain::radius+Wire::radius;
   r *= winchRadius;
   vec3 end (r*cos(a),r*sin(a), pourHeight+dz);
   // Boundary condition: constant force
   v4sf boundaryForce = Wire::tensionStiffness
     * float3(replacementLength-Wire::internodeLength);
   v4sf relativePosition = end - wire.position[wire.count-1];
   length = sqrt(sq3(relativePosition));
   //v4sf x = length - Wire::internodeLength4;
   //endTension = Wire::tensionStiffness * x;
   direction = relativePosition/length;
    wire.force[wire.count-1] += boundaryForce * direction;
    //wire.force[0] += boundaryForce * r2/sqrt(sq3(r2));
    //v4sf r2 (vec3(winchRadius,0,2*Grain::radius/*Wire::radius*/)-wire.position[0]);
    //wire.force[0] += Wire::tensionStiffness * r2;
    //wire.velocity[0] = _0f;
    //wire.position[0] = vec3(winchRadius,0,Grain::radius+dz/*Wire::radius*/);
  }

  wireIntegrationTime.start();
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) {
    wire.step(i);
    // FIXME: For correct truncate indexing
    min = ::min(min, wire.position[i]);
    max = ::max(max, wire.position[i]);
   }
  }, 1);
  wireIntegrationTime.stop();
  wireTime.stop();

  grainTime.start();
  grainIntegrationTime.start();
  parallel_chunk(grain.count, [this](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) {
    grain.step(i);
    min = ::min(min, grain.position[i]);
    max = ::max(max, grain.position[i]);
   }
  }, 1);
  grainIntegrationTime.stop();
  grainTime.stop();

  // kT
  v4sf ssqV = _0f;
  if(grain.count) for(v4sf v: grain.velocity.slice(0, grain.count)) ssqV += sq3(v);
  kT = 1./2*grain.mass*ssqV[0];

  if(load.count) {
   load.force[0][0]=0;
   load.force[0][1]=0;
   load.step(0);
   assert_(load.velocity[0][0]==0 &&load.velocity[0][1]==0);
   //assert_(load.position[0][0]==0 &&load.position[0][1]==0,);
   if(!(load.position[0][0]==0 &&load.position[0][1]==0)) {
    log(load.position, load.velocity, load.force);
    load.position[0][0]=0;
    load.position[0][1]=0;
   }
   if(processState==Load) {
    if(kT < 64 * grain.count * grain.mass * (Wire::radius*Wire::radius) /*/ sq(T)*/) {
     processState=Release;
     log("Release", kT / (grain.count * grain.mass * (Wire::radius*Wire::radius)));
   }
    //log(load.velocity[0][2], kT / (grain.count * grain.mass[0] * (Wire::radius*Wire::radius)));
    //if(load.force[0][2] > load.mass[0] * g[2])
    // Caps load velocity
    float maxZ = 0, vz=0;
    for(size_t i: range(grain.count)) {
     auto p = grain.position[i];
     if(p[2]>maxZ) {
      maxZ = p[2];
      vz = grain.velocity[i][2];
     }
    }
    if(load.position[0][2] < maxZ+Grain::radius+Grain::radius)
     load.velocity[0][2] = ::max(-1.f/2/T, load.velocity[0][2]);
   }
  }

  //log(staticFrictionCount, dynamicFrictionCount);
  staticFrictionCount2 = staticFrictionCount;
  dynamicFrictionCount2 = dynamicFrictionCount;
  staticFrictionCount = 0; dynamicFrictionCount = 0;

  v4sf wireLength = _0f;
  for(size_t i: range(wire.count-1))
   wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
  float stretch = (wireLength[0] / wire.count) / Wire::internodeLength;
  if(stretch>1.1) log(stretch);

  //wire.position[0] = vec3(winchRadius, 0, Wire::radius); // Anchors first end
  if(processState==Pour) { // Generates wire (winch)

   if(length[0] >= replacementLength) {
    Locker lock(this->lock);
    size_t i = wire.count++;
    wire.position[i] = wire.position[wire.count-2]
      + float3(replacementLength) * direction;
    //log(wire.count, direction, wire.position[wire.count-2], wire.position[i]);
    min = ::min(min, wire.position[i]);
    max = ::max(max, wire.position[i]);
    wire.velocity[i] = _0f;
    for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
    wire.frictions.set(i);
   }
  }

  timeStep++;
  winchAngle += winchRate * dt;

  stepTime.stop();
  if(0) {
   Locker lock(this->lock);
   size_t i = 0;
   if(1) {
    if(i>=plots.size) plots.append();

    v4sf sumV = _0f;
    for(v4sf v: grain.velocity.slice(0, grain.count)) sumV += sqrt(sq3(v));
    /*plots[1].dataSets["v"__][timeStep*dt] =*/
    //log(sumV[0] / grain.count); // max~0.13
    plots[i].dataSets["Et"__][timeStep*dt] = kT;
    if(!isNumber(kT)) { log("!isNumber(kT)"); kT=0; }
    assert(isNumber(kT));
    float ssqR = 0;
    if(grain.count) for(auto v: grain.angularVelocity.slice(0, grain.count)) {
     //if(!isNumber(v)) continue; //FIXME
     //assert(isNumber(v));
     ssqR += sq3(v)[0];
    }
    kR = 1./2*grain.angularMass*ssqR;
    if(!isNumber(kR)) { /*log("!isNumber(kR)");FIXME*/ kR=0; }
    assert(isNumber(kR));
    plots[i].dataSets["Er"__][timeStep*dt] = kR;
    //plots[i].dataSets["Eg"__][timeStep*dt] = gravityEnergy;
    plots[i].dataSets["Ete"__][timeStep*dt] = tensionEnergy;
    plots[i].dataSets["Eto"__][timeStep*dt] = torsionEnergy;
    plots[i].dataSets["Ec"__][timeStep*dt] = contactEnergy;
    //plots[i].dataSets["Em"__][timeStep*dt] = kT+kR+gravityEnergy+tensionEnergy+contactEnergy;
    i++;
   }
   if(processState < Done) {
    if(i>=plots.size) plots.append();
    float h = load.count ? load.position[0][2] : pourHeight;
    plots[i].dataSets["P"__][timeStep*dt] = side.force.radialSum /
      (2*PI*side.castRadius*h);
    i++;
   }
  }
 }
};
