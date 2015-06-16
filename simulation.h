#include "system.h"

struct Simulation : System {
 struct Lattice {
  float scale;
  int3 size;
  buffer<uint16> indices;
  Lattice(float radius, int3 size) : scale(sqrt(3.)/(2*radius)), size(size),
    indices(size.z*size.y*size.x) { indices.clear(); }
 } lattice {Grain::radius, int3(32,32,32)};

 // Process
 const float pourRadius = side.castRadius /*- Wire::radius*/ - Grain::radius;
 const real winchRadius = side.castRadius - 2*Grain::radius - Wire::radius;
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
 Time grainInitializationTime, grainContactTime, grainIntegrationTime;
 Time wireContactTime, wireFrictionTime, wireIntegrationTime;
 Time wireBoundTime, wireTensionTime;

 Lock lock;
 HList<Plot> plots;

 vec3f winchPosition(real winchAngle = -1) {
  if(winchAngle<0) winchAngle = this->winchAngle;
  real r = 1;
  if(loopAngle) {
    real t = mod(winchAngle / loopAngle, 4);
    /**/  if(/*0 <*/ t < 1) r = 1;
    else if(/*1 <*/ t < 2) r = 1-2*(t-1);
    else if(/*2 <*/ t < 3) r = -1;
    else    /*3 <    t < 4*/r = -1+2*(t-3);
 }
  return vec3f(winchRadius*r*cos(winchAngle),
                      winchRadius*r*sin(winchAngle),
                      pourHeight+2*Grain::radius);
 }

 Simulation() {
  if(useWire) {
   size_t i = wire.count++;
   vec3f pos = winchPosition();
   wire.position[i] = pos;
   wire.velocity[i] = 0;
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = 0;
   wire.frictions.set(i);
  }
  if(rollTest) for(;grain.count < grain.capacity;) {
   vec3f p(random()*2-1,random()*2-1, pourHeight);
   vec3f newPosition (pourRadius*p.xy(), p.z);
   size_t i = grain.count++;
   grain.position[i] = newPosition;
   grain.velocity[i] = 0;
   for(size_t n: range(3)) grain.positionDerivatives[n][i] = 0;
   for(size_t n: range(3)) grain.angularDerivatives[n][i] = 0;
   grain.frictions.set(i);
   real t0 = 2*PI*random();
   real t1 = acos(1-2*random());
   real t2 = (PI*random()+acos(random()))/2;
   grain.rotation[i] = {float(cos(t2)), vec3f(sin(t0)*sin(t1)*sin(t2),
                        cos(t0)*sin(t1)*sin(t2),
                        cos(t1)*sin(t2))};
   grain.angularVelocity[i] = 0;
   grain.rotation[i] = quat{1, 0};
   if(grain.capacity == 1 && i == 0) { // Roll test
    grain.velocity[0] = vec3f((- grain.position[0] / T).xy(), 0);
    log(grain.velocity[0]);
   }
   else if(grain.capacity == 2 && i == 1) { // Collision test
    grain.velocity[1] = (grain.position[0] - grain.position[1]) / T;
    grain.velocity[1] /= ::length(grain.velocity[1]);
   }
  }
 }

 vec3 tension(size_t a, size_t b) {
  vec3f relativePosition = wire.position[a] - wire.position[b];
  float length = ::length(relativePosition);
  float x = length - Wire::internodeLength;
  vec3f direction = relativePosition/length;
  potentialEnergy += 1./2 * Wire::tensionStiffness * sq(x);
  real fS = - Wire::tensionStiffness * x;
  vec3f relativeVelocity = wire.velocity[0][a] - wire.velocity[0][b];
  real fB = - wire.tensionDamping * dot(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 void step() {
  stepTime.start();
  // Process
  //Locker lock(this->lock);
  if(processState == Pour && pourHeight >= lattice.size.z/lattice.scale) {
   log("Max height");
   processState = Load;
  }
  if(processState == Pour) {
   if(pourHeight>=2 || grain.count == grain.capacity
               || (wire.capacity && wire.count == wire.capacity)) {
    log("Poured", "Grain", grain.count, "Wire", wire.count);
    processState=Load;
   } else {
    // Generates falling grain (pour)
    const bool pourGrain = true;
    if(pourGrain) for(;;) {
     vec3f p(random()*2-1,random()*2-1, pourHeight);
     if(length(p.xy())>1) continue;
     vec3f newPosition (pourRadius*p.xy(), p.z); // Within cylinder
     for(vec3f p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     for(vec3f p: grain.position.slice(0, grain.count))
      if(length(p - newPosition) < Grain::radius+Grain::radius)
       goto break2_;
     Locker lock(this->lock);
     size_t i = grain.count++;
     grain.position[i] = newPosition;
     grain.velocity[i] = 0;
     for(size_t n: range(3)) grain.positionDerivatives[n][i] = 0;
     grain.angularVelocity[i] = 0;
     for(size_t n: range(3)) grain.angularDerivatives[n][i] = 0;
     grain.frictions.set(i);
     real t0 = 2*PI*random();
     real t1 = acos(1-2*random());
     real t2 = (PI*random()+acos(random()))/2;
     grain.rotation[i] = {float(cos(t2)), vec3f( sin(t0)*sin(t1)*sin(t2),
                          cos(t0)*sin(t1)*sin(t2),
                          cos(t1)*sin(t2))};
     break;
    }
break2_:;
    pourHeight += winchSpeed * dt;
    if(useWire) { // Generates wire (winch)
     winchAngle += winchRate * dt;

     vec3f lastPosition = wire.count ? wire.position[wire.count-1] : winchPosition(0);
     vec3f r = winchPosition() - lastPosition;
     float l = length(r);
     if(l > Wire::internodeLength*(1+1./4/*2*/)) {
      Locker lock(this->lock);
      size_t i = wire.count++;
      vec3f pos = lastPosition + float(Wire::internodeLength)/l*r;
      wire.position[i] = pos;
      wire.velocity[i] = pos;
      for(size_t n: range(3)) wire.positionDerivatives[n][i] = 0;
      wire.frictions.set(i);
     }
    }
   }
  }
  if(processState == Load && !load.count) {
   Locker lock(this->lock);
   load.count++;
   float maxZ = 0;
   for(vec3f p: grain.position.slice(0, grain.count)) maxZ = ::max(maxZ, p.z);
   load.position[0] = vec3f(0,0,maxZ+Grain::radius);
   load.velocity[0] = 0;
   for(size_t n: range(3)) load.positionDerivatives[n][0] = 0;
  }
  if(processState==Release) {
   if(side.castRadius < 16*Grain::radius) {
    side.castRadius += dt * (1./16 * L/T); // Constant velocity
    // + Pressure
    float h = load.count ? load.position[0].z : pourHeight;
    float P = side.force.radialSum / (2*PI*side.castRadius*h);
    side.castRadius += dt * (1./16 * L/T) * P / (M*L/T/T/(L*L));
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

  grainContactTime.start();
  parallel_chunk(lattice.indices.size, [this](uint, size_t start, size_t size) {
   const int Y = lattice.size.y, X = lattice.size.x;
   const uint16* chunk = lattice.indices.begin() + start;
   assert_(start==0);
   const uint16* end = lattice.indices.end();
   for(size_t i: range(size)) {
    const uint16* current = chunk + i;
    size_t a = *current;
    if(!a) continue;
    a--;
    for(int z: range(2+1)) for(int y: range(-2, 2+1)) for(int x: range(-2, 2+1)) {
     int di = z*Y*X + y*X + x;
     if(di <= 0) continue; // FIXME: unroll
     const uint16* neighbour = current + z*Y*X + y*X + x;
     if(neighbour >= end) continue;
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
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   uint16* begin = lattice.indices.begin(), *end = lattice.indices.end();\
   const int X = lattice.size.x, Y = lattice.size.y;
   uint16* origin = begin;// + Y/2*X + X/2;
   for(size_t i: range(start, start+size)) {
    // Gravity
    wire.force[i] = wire.mass * g;
    if(i > 0) { // Tension
     vec3 fT = tension(i-1, i);
     if(i > start) wire.force[i-1] += fT; // else done as last of previous chunk
     wire.force[i] -= fT;
    }
    // Bounds
    penalty(wire, i, floor, 0);
    // Wire - Grain
    int3 p (vec3f(X/2,Y/2,0)+ lattice.scale*wire.position[i]);
    const uint16* current = origin + p.z*Y*X + p.y*X + p.x;
    static_assert(Wire::radius < (/*Lattice size 2/√3*/2/1.8/*sqrt(3.)*/-1)*Grain::radius, "");
    for(int z: range(-1, 2)) for(int y: range(-1, 2)) for(int x: range(-1, 2)) {
     const uint16* neighbour = current + z*Y*X + y*X + x;
     if(neighbour < begin || neighbour >= end) continue;
     size_t b = *neighbour;
     if(!b) continue;
     b--;
     penalty(wire, i, grain, b);
    }
   }
   // Tension to first node of next chunk
   if(start+size < wire.count)
    wire.force[start+size-1] += tension(start+size-1, start+size);
  }, 1);

  // Torsion springs (Bending resistance)
  if(wireBendStiffness) for(size_t i: range(1, wire.count-1)) {
   vec3f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
   vec3f a = C-B, b = B-A;
   vec3f c = cross(a, b);
   float l = length(c);
   if(l) {
    float p = atan(l, dot(a, b));
    vec3f dap = cross(a, cross(a,b)) / (sq(a) * l);
    vec3f dbp = cross(b, cross(b,a)) / (sq(b) * l);
    wire.force[i+1] += wireBendStiffness * vec3(-p*dap);
    wire.force[i] += wireBendStiffness * vec3(p*dap - p*dbp);
    wire.force[i-1] += wireBendStiffness * vec3(p*dbp);
    if(1) {
     vec3f A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
     vec3f axis = cross(C-B, B-A);
     if(axis) {
      float angularVelocity = atan(length(axis), dot(C-B, B-A));
      wire.force[i] += wireBendDamping * angularVelocity
        * vec3(cross(axis/length(axis), C-A) / float(2));
     }
    }
   }
  }

  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) wire.step(i);
  }, 1);
  wireTime.stop();

  grainTime.start();
  lattice.indices.clear(0);
  parallel_chunk(grain.count, [this](uint, size_t start, size_t size) {
   const int X = lattice.size.x, Y = lattice.size.y;
   uint16* origin = lattice.indices.begin();// + Y/2*X + X/2;
   for(size_t i: range(start, start+size)) {
    grainIntegrationTime.start();
    grain.step(i);
    grainIntegrationTime.stop();
    int3 p (vec3f(X/2,Y/2,0)+ lattice.scale*grain.position[i]);
    uint16* current = origin + p.z*Y*X + p.y*X + p.x;
    /*if(p.z < 0 || p.y < 0 || p.x < 0 || p.z >= lattice.size.z || p.y >= Y || p.x >= X) {
     log(i); continue;
    }*/
    *current = 1+i;
   }
  }, 1);
  grainTime.stop();

  if(load.count) {
   load.step(0);
   if(kT < 1./8 && processState==Load) {
    processState=Release;
    log("Release");
   }
  }

  //log(staticFrictionCount, dynamicFrictionCount);
  staticFrictionCount = 0; dynamicFrictionCount = 0;

  timeStep++;

  if(0) {
   Locker lock(this->lock);
   size_t i = 0;
   if(1) {
    if(i>=plots.size) plots.append();
    real sumV = 0, ssqV = 0;
    if(grain.count) for(vec3f v: grain.velocity.slice(0, grain.count)) {
     if(!isNumber(v)) continue; //FIXME
     assert(isNumber(v));
     sumV += length(v);
     ssqV += sq(v);
    }
    kT = 1./2*grain.mass*ssqV;
    //plots[1].dataSets["v"__][timeStep*dt] = sumV / grain.count;
    plots[i].dataSets["Et"__][timeStep*dt] = kT;
    if(!isNumber(kT)) { log("!isNumber(kT)"); kT=0; }
    assert(isNumber(kT));
    real ssqR = 0;
    if(grain.count) for(vec3f v: grain.angularVelocity.slice(0, grain.count)) {
     if(!isNumber(v)) continue; //FIXME
     assert(isNumber(v));
     ssqR += sq(v);
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
    float h = load.count ? load.position[0].z : pourHeight;
    plots[i].dataSets["P"__][timeStep*dt] = side.force.radialSum /
      (2*PI*side.castRadius*h);
    i++;
   }
  }
  stepTime.stop();
 }
};
