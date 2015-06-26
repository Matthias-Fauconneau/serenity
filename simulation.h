#include "system.h"
#include "time.h"

struct Lattice {
 const vec4f scale;
 const vec4f min, max;
 const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
 buffer<uint16> indices {size_t(size.z*size.y*size.x)};
 const vec4f XYZ0 {float(1), float(size.x), float(size.y*size.x), 0};
 Lattice(float scale, vec4f min, vec4f max) :
   scale(float3(scale)),
   min(min - float3(2./scale)),
   max(max + float3(2./scale)) { // Avoids bound check
  indices.clear(0);
 }
 inline size_t index(vec4f p) {
  return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0];
 }
 inline uint16& operator()(vec4f p) {
  return indices[index(p)];
 }
};

struct Simulation : System {
 vec4f min = _0f, max = _0f;

 // Process
 const float pourRadius = side.castRadius - Grain::radius - Wire::radius;
 const float winchRadius = side.castRadius - Grain::radius - Wire::radius;
 float pourHeight = Floor::height+Grain::radius;
 float winchAngle = 0;

 Random random;
 enum { Pour, Release, Wait, Load, Done, Fail } processState = Pour;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
 Time wireLatticeTime, wireContactTime, wireIntegrationTime;

 Lock lock;
 File file;

 Simulation(const Dict& p, File&& file) : System(p), file(move(file)) {
  // Initial wire node
  size_t i = wire.count++;
  wire.position[i] = vec3(winchRadius,0,pourHeight);
  min = ::min(min, wire.position[i]);
  max = ::max(max, wire.position[i]);
  wire.velocity[i] = _0f;
  for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
  wire.frictions.set(i);
 }

 inline vec4f tension(size_t a, size_t b) {
  vec4f relativePosition = wire.position[a] - wire.position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - Wire::internodeLength4;
  tensionEnergy += 1./2 * wire.tensionStiffness[0] * sq3(x)[0];
  vec4f fS = - wire.tensionStiffness * x;
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = wire.velocity[a] - wire.velocity[b];
  vec4f fB = - wire.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 void step() {
  stepTime.start();
  // Process
  if(processState == Pour) {
   if(pourHeight>=2 || grain.count == grain.capacity
               || (wire.capacity && wire.count == wire.capacity)) {
    float wireDensity = (wire.count-1)*Wire::volume / ((grain.count)*Grain::volume);
    //file.write(str("grain.count:", grain.count, "wire.count", wire.count, "wireDensity:"wireDensity)+"\n");
    if(file) file.write(str(grain.count, wire.count, wireDensity)+"\n");
    log("Pour->Release", grain.count, wire.count, wireDensity);
    processState++;
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && pourHeight >= Grain::radius) for(;;) {
     vec4f p {random()*2-1,random()*2-1, pourHeight, 0};
     if(length2(p)>1) continue;
     vec4f newPosition {pourRadius*p[0], pourRadius*p[1], p[2], 0}; // Within cylinder
     // Finds lowest Z (reduce fall/impact energy)
     newPosition[2] = Grain::radius;
     for(auto p: grain.position.slice(0, grain.count)) {
      float x = length(toVec3(p - newPosition).xy());
      if(x < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius) - sq(x));
       newPosition[2] = ::max(newPosition[2], p[2]+dz);
      }
     }
     if(newPosition[2] > pourHeight) break;
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
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
  if(processState == Release && !load.count) {
   float maxZ = 0, vz=0;
   for(size_t i: range(grain.count)) {
    auto p = grain.position[i];
    if(p[2]>maxZ) {
     maxZ = p[2];
     vz = grain.velocity[i][2];
    }
   }
   if(vz > -1) {
    Locker lock(this->lock);
    load.count++;
    load.position[0] = vec3(0,0,maxZ+Grain::radius+Grain::radius);
    load.velocity[0] = _0f;
    for(size_t n: range(3)) load.positionDerivatives[n][0] = _0f;
   }
  }
  if(processState==Release) {
   if(side.castRadius < 15*Grain::radius) {
    static float releaseTime = timeStep*dt;
    side.castRadius = side.initialRadius * exp((timeStep*dt-releaseTime)/(1*s));
   } else {
    log("Release->Wait");
    processState = Wait;
   }
  }
  if(processState == Wait && kineticEnergy <
     128 * grain.count * grain.mass * (Wire::radius*Wire::radius) / sq(s)) {
   processState = Load;
  }
  if(processState == Load && load.position[0][2] <= 2*2*Grain::radius) {
   processState = Done;
  }

  // Initialization
  normalEnergy = 0, staticEnergy = 0, tensionEnergy = 0, bendEnergy = 0;

  // Load
  if(load.count) load.force[0] = float4(load.mass) * G;

  // Grain
  grainTime.start();

  vec4f size = float4(sqrt(3.)/(2*Grain::radius))*(max-min);
  if(size[0]*size[1]*size[2] > 256*256*256) {  // 64 MB
   log("Domain too large", min, max, size);
   processState = Fail;
   return;
  }
  Lattice grainLattice(sqrt(3.)/(2*Grain::radius), min, max);

  parallel_chunk(grain.count, [this,&grainLattice](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    grain.force[a] = float4(grain.mass) * G;
    grain.torque[a] = _0f;
    penalty(grain, a, floor, 0);
    if(load.count) {
     penalty(grain, a, load, 0);
    }
    if(processState <= Release) penalty(grain, a, side, 0);
    grainLattice(grain.position[a]) = 1+a;
   }
  }, 1);
  grainInitializationTime.stop();

  grainContactTime.start();
  parallel_chunk(grainLattice.indices.size,
                 [this,&grainLattice](uint, size_t start, size_t size) {
   const int Y = grainLattice.size.y, X = grainLattice.size.x;
   const uint16* chunk = grainLattice.indices.begin() + start;
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
  }, threadCount);
  grainContactTime.stop();
  grainTime.stop();

  // Wire
  wireTime.start();

  wireContactTime.start();
  if(wire.count) parallel_chunk(wire.count,
                                [this,&grainLattice](uint, size_t start, size_t size) {
   //assert(size>1);
   uint16* grainBase = grainLattice.indices.begin();
   const int gX = grainLattice.size.x, gY = grainLattice.size.y;
   const uint16* grainNeighbours[3*3] = {
    grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
    grainBase-gX-1, grainBase-1, grainBase+gX-1,
    grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
   };

   wire.force[start] = float4(wire.mass) * G;
   { // First iteration
    size_t i = start;
    wire.force[i+1] = float4(wire.mass) * G;
    if(i > 0) {
     if(start > 1 && wire.bendStiffness) {// Previous torsion spring on first
      size_t i = start-1;
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f l = sqrt(sq3(c));
      if(l[0]) {
       float p = wire.bendStiffness * atan(l[0], dot3(a, b)[0]);
       vec4f dap = cross(a, cross(a,b)) / (sq3(a) * l);
       wire.force[i+1] += float3(p) * (-dap);
      }
     }
     // First tension
     vec4f fT = tension(i-1, i);
     wire.force[i] -= fT;
     if(wire.bendStiffness) { // First torsion
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f l = sqrt(sq3(c));
      if(l[0]) {
       float p = wire.bendStiffness * atan(l[0], dot3(a, b)[0]);
       vec4f dap = cross(a, cross(a,b)) / (sq3(a) * l);
       vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * l);
       wire.force[i+1] += float3(p) * (-dap);
       wire.force[i] += float3(p) * (dap - dbp);
        //wire.force[i-1] += float3(p) * (dbp); //-> "Tension to first node of next chunk"
       if(1) {
        vec4f A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
        vec4f axis = cross(C-B, B-A);
        vec4f l = sqrt(sq3(axis));
        if(l[0]) {
         float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
         wire.force[i] += float3(Wire::bendDamping * angularVelocity / 2)
           * cross(axis/l, C-A);
        }
       }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    if(processState <= Release) penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
      if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
       }}
   }
   for(size_t i: range(start+1, start+size-1)) {
    // Gravity
    wire.force[i+1] = float4(wire.mass) * G;
    // Tension
    vec4f fT = tension(i-1, i);
    wire.force[i-1] += fT;
    wire.force[i] -= fT;
     if(wire.bendStiffness) {// Torsion springs (Bending resistance)
     vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     vec4f a = C-B, b = B-A;
     vec4f c = cross(a, b);
     vec4f length4 = sqrt(sq3(c));
     float length = length4[0];
     if(length) {
      vec4f p = float3(wire.bendStiffness * atan(length, dot3(a, b)[0]));
      vec4f dap = cross(a, cross(a,b)) / (sq3(a) * length4);
      vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * length4);
      wire.force[i+1] += p* (-dap);
      wire.force[i] += p * (dap - dbp);
      wire.force[i-1] += p * (dbp);
      if(1) {
       vec4f A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
       vec4f axis = cross(C-B, B-A);
       vec4f length4 = sqrt(sq3(axis));
       float length = length4[0];
       if(length) {
        float angularVelocity = atan(length, dot3(C-B, B-A)[0]);
        wire.force[i] += float3(Wire::bendDamping * angularVelocity / 2)
          * cross(axis/length4, C-A);
       }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    if(processState <= Release) penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
   }

   // Last iteration
   if(size>1) {
    size_t i = start+size-1;
    // Gravity
    wire.force[i] = float4(wire.mass) * G;
     // Tension
    vec4f fT = tension(i-1, i);
    wire.force[i-1] += fT;
    wire.force[i] -= fT;
    // Torsion with next chunk
    if(i+1 < wire.count) {
     vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     vec4f a = C-B, b = B-A;
     vec4f c = cross(a, b);
     vec4f l = sqrt(sq3(c));
     if(l[0]) {
      float angle = atan(l[0], dot3(a, b)[0]);
      //torsionEnergy += 1./2 * wire.bendStiffness * L * sq(angle);
      vec4f dap = cross(a, cross(a,b)) / (sq3(a) * l);
      vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * l);
      //wire.force[i+1] += float3(p) * (-dap); //-> "Previous torsion spring on first"
      float p = wire.bendStiffness * angle;
      wire.force[i] += float3(p) * (dap - dbp);
      wire.force[i-1] += float3(p) * (dbp);
      if(1) {
       vec4f A = wire.velocity[i-1], B = wire.velocity[i], C = wire.velocity[i+1];
       vec4f axis = cross(C-B, B-A);
       vec4f l = sqrt(sq3(axis));
       if(l[0]) {
        float angularVelocity = atan(l[0], dot3(C-B, B-A)[0]);
        wire.force[i] += float3(Wire::bendDamping * angularVelocity / 2)
          * cross(axis/l, C-A);
        }
      }
     }
    }
    // Bounds
    penalty(wire, i, floor, 0);
    if(processState <= Release) penalty(wire, i, side, 0);
    if(load.count) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
   }

   { // Tension to first node of next chunk
    size_t i = start+size;
    if(i < wire.count) {
     wire.force[i-1] += tension(i-1, i);
      // Torsion to first node of next chunk
     if(i+1<wire.count && wire.bendStiffness) {
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f l = sqrt(sq3(c));
      if(l[0]) {
       float p = wire.bendStiffness * atan(l[0], dot3(a, b)[0]);
       vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * l);
       wire.force[i-1] += float3(p) * (dbp);
      }
     }
    }
   }
  }, ::threadCount);
  wireContactTime.stop();

  // Integration
  min = _0f; max = _0f;

  wireIntegrationTime.start();
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) {
    System::step(wire, i);
    // for correct truncate indexing
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
    System::step(grain, i);
    min = ::min(min, grain.position[i]);
    max = ::max(max, grain.position[i]);
   }
  }, 1);
  grainIntegrationTime.stop();
  grainTime.stop();

  // kT
  vec4f ssqV = _0f;
  if(grain.count) for(vec4f v: grain.velocity.slice(0, grain.count)) ssqV += sq3(v);
  kineticEnergy = 1./2*grain.mass*ssqV[0];

  if(load.count) {
   load.force[0][0]=0;
   load.force[0][1]=0;
   System::step(load, 0);
   if(processState==Load) {
    if(timeStep%subStepCount == 0) { // Records results every 1/60s
     v4sf wireLength = _0f;
     for(size_t i: range(wire.count-1))
      wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
     float stretch = (wireLength[0] / wire.count) / Wire::internodeLength;
     if(file) file.write(str(load.mass, load.position[0][2], tensionEnergy, stretch)+'\n');
    }
    load.mass *= 1 + dt * 1/s; // 2x /s
   }
  }

  if(processState==Pour) { // Generates wire (winch)
   float a = winchAngle, r = 1, dz = 0;
   if(loopAngle) {
    float t = mod(winchAngle / loopAngle, 4);
    float b = (winchAngle - t*loopAngle)/2;
    /**/  if(/*0 <*/ t < 1) r = 1, a=b+t*loopAngle;
    else if(/*1 <*/ t < 2) r = 1-2*(t-1), a=b+loopAngle;
    else if(/*2 <*/ t < 3) r = -1, a=b+loopAngle+/*-*/(t-2)*loopAngle;
    else    /*3 <    t < 4*/r = -1+2*(t-3), a=b+2*loopAngle;
   }
   dz = Grain::radius+Wire::radius;
   r *= winchRadius;
   vec3 end (r*cos(a),r*sin(a), pourHeight+dz);
   vec4f relativePosition = end - wire.position[wire.count-1];
   vec4f length = sqrt(sq3(relativePosition));
   if(length[0] >= Wire::internodeLength) {
    Locker lock(this->lock);
    size_t i = wire.count++;
    wire.position[i] = wire.position[wire.count-2]
      + float3(Wire::internodeLength) * relativePosition/length;
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
  /*if(0) {
   //Locker lock(this->lock);
   while(plots.size < 2) plots.append();
   plots[0].dataSets["kinetic"__][timeStep*dt] = kineticEnergy;
   plots[0].dataSets["normal"__][timeStep*dt] = normalEnergy;
   plots[0].dataSets["static"__][timeStep*dt] = staticEnergy;
   plots[0].dataSets["tension"__][timeStep*dt] = tensionEnergy;
   plots[0].dataSets["bend"__][timeStep*dt] = bendEnergy;
   if(load.count) plots[1].dataSets["height"__][timeStep*dt] = load.position[0][2];
  }*/
 }
};
