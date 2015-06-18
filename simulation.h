#include "system.h"

struct Simulation : System {
 v4sf min = _0f, max = _0f;

 // Process
 const float pourRadius = side.castRadius - Grain::radius - Wire::radius;
 const real winchRadius = side.castRadius - (loopAngle?Grain::radius:Wire::radius);
 real pourHeight = Floor::height+Grain::radius;
 real winchAngle = 0;

 real kT=0, kR=0;

 Random random;
 enum { Pour, Load, Release, Done } processState = rollTest ? Done : Pour;
 int direction = 1, directionSwitchCount = 0;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
 Time wireContactTime, wireIntegrationTime, wireTensionTime;

 Lock lock;
 HList<Plot> plots;

 Simulation() {
  //error(cross(v4sf{1,0,0,0}, v4sf{0,1,0,0}));
  if(useWire) {
   size_t i = wire.count++;
   wire.position[i] = vec3f(winchRadius,0,pourHeight);
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
   wire.velocity[i] = _0f;
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
   wire.frictions.set(i);
  }
  if(rollTest) for(;grain.count < grain.capacity;) {
   vec3f p(random()*2-1,random()*2-1, pourHeight);
   vec3f newPosition (pourRadius*p.xy(), p.z);
   size_t i = grain.count++;
   grain.position[i] = newPosition;
   grain.velocity[i] = _0f;
   for(size_t n: range(3)) grain.positionDerivatives[n][i] = _0f;
   for(size_t n: range(3)) grain.angularDerivatives[n][i] = _0001f;
   grain.frictions.set(i);
   real t0 = 2*PI*random();
   real t1 = acos(1-2*random());
   real t2 = (PI*random()+acos(random()))/2;
   grain.rotation[i] = {sin(t0)*sin(t1)*sin(t2),
                                  cos(t0)*sin(t1)*sin(t2),
                                  cos(t1)*sin(t2),
                                  cos(t2)};
   grain.angularVelocity[i] = _0f;
   grain.rotation[i] = _0001f;
   /*if(grain.capacity == 1 && i == 0) { // Roll test
    grain.velocity[0] = vec3f((- grain.position[0] / T).xy(), 0);
    log(grain.velocity[0]);
   }
   else if(grain.capacity == 2 && i == 1) { // Collision test
    grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
    grain.velocity[1] /= ::length(grain.velocity[1]);
   }*/
  }
 }

 inline v4sf tension(size_t a, size_t b) {
  v4sf relativePosition = wire.position[a] - wire.position[b];
  v4sf length = sqrt(sq3(relativePosition));
  v4sf x = length - Wire::internodeLength4;
  //potentialEnergy += 1./2 * Wire::tensionStiffness * sq(x);
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
     vec3f p(random()*2-1,random()*2-1, pourHeight);
     if(length(p.xy())>1) continue;
     vec3f newPosition (pourRadius*p.xy(), p.z); // Within cylinder
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     for(auto p: grain.position.slice(0, grain.count))
      if(length(p - newPosition) < Grain::radius+Grain::radius)
       goto break2_;
     Locker lock(this->lock);
     size_t i = grain.count++;
     grain.position[i] = newPosition;
     min = ::min(min, grain.position[i]);
     max = ::max(max, grain.position[i]);
     grain.velocity[i] = _0f;
     for(size_t n: range(3)) grain.positionDerivatives[n][i] = _0f;
     grain.angularVelocity[i] = _0f;
     for(size_t n: range(3)) grain.angularDerivatives[n][i] = _0001f;
     grain.frictions.set(i);
     real t0 = 2*PI*random();
     real t1 = acos(1-2*random());
     real t2 = (PI*random()+acos(random()))/2;
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
   Locker lock(this->lock);
   load.count++;
   float maxZ = 0;
   for(auto p: grain.position.slice(0, grain.count)) maxZ = ::max(maxZ, p[2]);
   load.position[0] = vec3f(0,0,maxZ+Grain::radius);
   load.velocity[0] = _0f;
   for(size_t n: range(3)) load.positionDerivatives[n][0] = _0f;
  }
  if(processState==Release) {
   if(side.castRadius < 16*Grain::radius) {
    side.castRadius += dt * (16*Grain::radius/T); // Constant velocity
    /*// + Pressure
    float h = load.count ? load.position[0][2] : pourHeight;
    float P = side.force.radialSum / (2*PI*side.castRadius*h);
    side.castRadius += dt * (1./16 * L/T) * P / (M*L/T/T/(L*L));*/
   } else {
    log("Released");
    processState = Done;
   }
  }

  // Initialization
  potentialEnergy = 0;
  side.force.radialSum = 0;

  // Load
  if(load.count) load.force[0] = load.mass * g;

  // Grain
  grainTime.start();

  grainInitializationTime.start();
  parallel_chunk(grain.count, [this](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    grain.force[a] = grain.mass * g;
    grain.torque[a] = penalty(grain, a, floor, 0);
    if(load.count) grain.torque[a] += penalty(grain, a, load, 0);
    if(processState != Done) grain.torque[a] += penalty(grain, a, side, 0);
   }
  }, 1);
  grainInitializationTime.stop();

  const v4sf scale = float3(sqrt(3.)/(2*Grain::radius));
  min -= float3(2./scale[0]), max += float3(2./scale[0]); // Avoids bound check
  int3 gridSize = int3(::floor(toVec3f(scale*(max-min))))+int3(1);
  buffer<uint16> indices (gridSize.z*gridSize.y*gridSize.x);
  //log(grain.count*(6+5)*sizeof(v4sf)/1024, wire.count*6*sizeof(v4sf)/1024);
  indices.clear(0);
  grainLatticeTime.start();
  parallel_chunk(grain.count, [&](uint, size_t start, size_t size) {
   const v4sf XYZ0 {float(1), float(gridSize.x), float(gridSize.y*gridSize.x), 0};
   for(size_t i: range(start, start+size)) {
    indices[dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(grain.position[i]-min))))[0]] = 1+i;
   }
  }, 1);
  grainLatticeTime.stop();

  grainContactTime.start();
  parallel_chunk(indices.size, [&](uint, size_t start, size_t size) {
   const int Y = gridSize.y, X = gridSize.x;
   const uint16* chunk = indices.begin() + start;
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

  wireTensionTime.start();
  if(wire.count) parallel_chunk(wire.count, [&](uint, size_t start, size_t size) {

   uint16* base = indices.begin();
   const int X = gridSize.x, Y = gridSize.y;
   const v4sf XYZ0 {1, float(X), float(Y*X), 0};
   const uint16* neighbours[3*3] = {
    base-X*Y-X-1, base-X*Y-1, base-X*Y+X-1,
    base-X-1, base-1, base+X-1,
    base+X*Y-X-1, base+X*Y-1, base+X*Y+X-1
   };

   { // First iteration
    size_t i = start;
    wire.force[i] = wire.mass * g;
    wire.force[i+1] = wire.mass * g;
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
       wire.force[i+1/*=start*/] += float4(p) * (-dap);
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
       wire.force[i+1] += float4(p) * (-dap);
       wire.force[i] += float4(p) * (dap - dbp);
       //wire.force[i-1] += float4(p) * (dbp); //-> "Tension to first node of next chunk"
       if(1) {
        v4sf A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
        v4sf axis = cross(C-B, B-A);
        v4sf l = sqrt(sq3(axis));
        if(l[0]) {
         float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
         wire.force[i] += float4(wireBendDamping * angularVelocity / 2)
           * cross(axis/l, C-A);
        }
       }
      }
     }
    }
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
   for(size_t i: range(start+1, start+size-1)) {
    // Gravity
    wire.force[i+1] = wire.mass * g;
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
      v4sf p = float4(wireBendStiffness * atan(length, dot3(a, b)[0]));
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
        wire.force[i] += float4(wireBendDamping * angularVelocity / 2)
          * cross(axis/length4, C-A);
       }
      }
     }
    }
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

   // Last iteration
   if(size>1) {
    size_t i = start+size-1;
    // Gravity
    wire.force[i] = wire.mass * g;
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
      float p = wireBendStiffness * atan(l[0], dot3(a, b)[0]);
      v4sf dap = cross(a, cross(a,b)) / (sq3(a) * l);
      v4sf dbp = cross(b, cross(b,a)) / (sq3(b) * l);
      //wire.force[i+1] += float4(p) * (-dap); //-> "Previous torsion spring on first"
      wire.force[i] += float4(p) * (dap - dbp);
      wire.force[i-1] += float4(p) * (dbp);
      if(1) {
       v4sf A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
       v4sf axis = cross(C-B, B-A);
       v4sf l = sqrt(sq3(axis));
       if(l[0]) {
        float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
        wire.force[i] += float4(wireBendDamping * angularVelocity / 2)
          * cross(axis/l, C-A);
       }
      }
     }
    }
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
       wire.force[i-1] += float4(p) * (dbp);
      }
     }
    }
   }
  }, 1/*::threadCount*/);
  wireTensionTime.stop();

  /*wireContactTime.start();
  // TODO: lattice split parallelization
  parallel_chunk(wire.count, [&](uint, size_t start, size_t size) {
   uint16* base = indices.begin();
   const int X = gridSize.x, Y = gridSize.y;
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

  // Integration
  min = _0f; max = _0f;

  // 1 N tension
  //log(Wire::tensionStiffness[0], 1/Wire::tensionStiffness[0]);
  const float N = 1;
  float replacementLength = Wire::internodeLength*(1+N/Wire::tensionStiffness[0]);
  v4sf length;
  v4sf direction;
  {
   //real r = (mod(winchAngle / loopAngle, 2) < 1 ? 1 : -1) * winchRadius;
   real r = 1, dz = 0;
   if(loopAngle) {
     real t = mod(winchAngle / loopAngle, 4);
     /**/  if(/*0 <*/ t < 1) r = 1;
     else if(/*1 <*/ t < 2) r = 1-2*(t-1);
     else if(/*2 <*/ t < 3) r = -1;
     else    /*3 <    t < 4*/r = -1+2*(t-3);
   }
   dz = Grain::radius+Wire::radius;
   r *= winchRadius;
   vec3f end (r*cos(winchAngle),r*sin(winchAngle), pourHeight+dz);
   // Boundary condition: constant force
   v4sf boundaryForce = Wire::tensionStiffness
     * float4(replacementLength-Wire::internodeLength);
   v4sf relativePosition = end - wire.position[wire.count-1];
   length = sqrt(sq3(relativePosition));
   //v4sf x = length - Wire::internodeLength4;
   //endTension = Wire::tensionStiffness * x;
   direction = relativePosition/length;
   if(useWire && processState==Pour) { // Pull wire
    wire.force[wire.count-1] += boundaryForce * direction;
    //wire.force[0] += boundaryForce * r2/sqrt(sq3(r2));
    //v4sf r2 (vec3f(winchRadius,0,2*Grain::radius/*Wire::radius*/)-wire.position[0]);
    //wire.force[0] += Wire::tensionStiffness * r2;
    //wire.velocity[0] = _0f;
    //wire.position[0] = vec3f(winchRadius,0,Grain::radius+dz/*Wire::radius*/);
   }
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
  kT = 1./2*grain.mass[0]*ssqV[0];

  if(load.count) {
   load.step(0);
   if(processState==Load) {
    if(kT < 32 * grain.count * grain.mass[0] * (Wire::radius*Wire::radius) /*/ sq(T)*/) {
     processState=Release;
     log("Release", kT / (grain.count * grain.mass[0] * (Wire::radius*Wire::radius)));
   }
    //log(load.velocity[0][2], kT / (grain.count * grain.mass[0] * (Wire::radius*Wire::radius)));
    if(load.force[0][2] > load.mass[0] * g[2])
     load.velocity[0][2] = ::max(-1/*6*Grain::radius*//T, load.velocity[0][2]); // Caps load velocity
   }
  }

  //log(staticFrictionCount, dynamicFrictionCount);
  staticFrictionCount = 0; dynamicFrictionCount = 0;

  v4sf wireLength = _0f;
  for(size_t i: range(wire.count-1))
   wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
  float stretch = (wireLength[0] / wire.count) / Wire::internodeLength;
  //if(stretch>1.1) log(stretch);

  //wire.position[0] = vec3(winchRadius, 0, Wire::radius); // Anchors first end
  if(useWire && processState==Pour) { // Generates wire (winch)

   if(length[0] >= replacementLength) {
    Locker lock(this->lock);
    size_t i = wire.count++;
    wire.position[i] = wire.position[wire.count-2]
      + float4(replacementLength) * direction;
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

  if(0) {
   Locker lock(this->lock);
   size_t i = 0;
   if(1) {
    if(i>=plots.size) plots.append();

    //plots[1].dataSets["v"__][timeStep*dt] = sumV / grain.count;
    plots[i].dataSets["Et"__][timeStep*dt] = kT;
    if(!isNumber(kT)) { log("!isNumber(kT)"); kT=0; }
    assert(isNumber(kT));
    real ssqR = 0;
    if(grain.count) for(auto v: grain.angularVelocity.slice(0, grain.count)) {
     //if(!isNumber(v)) continue; //FIXME
     //assert(isNumber(v));
     ssqR += sq3(v)[0];
    }
    kR = 1./2*grain.angularMass*ssqR;
    if(!isNumber(kR)) { /*log("!isNumber(kR)");FIXME*/ kR=0; }
    assert(isNumber(kR));
    plots[i].dataSets["Er"__][timeStep*dt] = kR;
    //plots[i].dataSets["Ep"__][timeStep*dt] = potentialEnergy;
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
  stepTime.stop();
 }
};
