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
   /*min(min - float3(4./scale)),
   max(max + float3(4./scale))*/
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

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, /*Release,*/ Wait, Load, Done, Fail };
static string processStates[] {"Pour","Wait","Load","Done"};

struct Simulation : System {
 // Process parameters
 const float pourHeight;
 const float pourRadius = side.initialRadius - Grain::radius - Wire::radius;
 const float winchRadius = side.initialRadius - Grain::radius - Wire::radius;
 const float loopAngle = PI*(3-sqrt(5.));
 sconst float winchSpeed = 1./2 *m/s; // TODO: ~ grain fall speed
 const float winchRate;
 const Pattern pattern;

 // Process variables
 float currentPourHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = winchRadius;
 float height = 0, initialHeight = 0;
 size_t lastCollapseReport = 0;
 Random random;
 ProcessState processState = Pour;

 // Lattice
 vec4f min = _0f, max = _0f;
 Lattice wireLattice{float(sqrt(3.)/(2*Wire::radius)),
    v4sf{-side.initialRadius,-side.initialRadius, -Grain::radius,0},
    v4sf{ side.initialRadius, side.initialRadius, pourHeight+2*Grain::radius,0}};

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
 Time wireLatticeTime, wireContactTime, wireIntegrationTime;

 Lock lock;
 Stream stream;
 Dict parameters;

 Simulation(const Dict& parameters, Stream&& stream) : System(parameters),
   pourHeight(parameters.at("Height"_)),
   winchRate(parameters.at("Rate"_)),
   pattern(Pattern(ref<string>(patterns).indexOf(parameters.at("Pattern")))),
   stream(move(stream)), parameters(copy(parameters)) {
  if(pattern) { // Initial wire node
   size_t i = wire.count++;
   wire.position[i] = vec3(winchRadius,0,currentPourHeight);
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
   wire.velocity[i] = _0f;
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
   wire.frictions.set(i);
  }
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

 // Formats vectors as flat lists without brackets nor commas
 String flat(const vec3& v) { return str(v[0], v[1], v[2]); }
 String flat(const v4sf& v) { return str(v[0], v[1], v[2]); }
 virtual void snapshot() {
  String name = str(parameters.at("Pattern"));
  {array<char> s;
   s.append(str(grain.radius)+'\n');
   s.append(str("X, Y, Z, grain contact count, grain contact energy (µJ), wire contact count, wire contact energy (µJ)")+'\n');
   buffer<int> wireCount(grain.count); wireCount.clear();
   buffer<float> wireEnergy(grain.count); wireEnergy.clear();
   for(size_t i : range(wire.count)) {
    for(auto& f: wire.frictions[i]) {
     if(f.energy && f.index >= grain.base && f.index < grain.base+grain.capacity) {
      wireCount[f.index-grain.base]++;
      wireEnergy[f.index-grain.base] += f.energy;
     }
    }
   }
   for(size_t i : range(grain.count)) {
    int grainCount=0; float grainEnergy=0;
    for(auto& f: grain.frictions[i]) {
     if(f.energy && f.index >= grain.base && f.index < grain.base+grain.capacity) {
      grainCount++;
      grainEnergy += f.energy;
     }
    }
    s.append(str(flat(grain.position[i]), grainCount, grainEnergy*1e6, wireCount[i], wireEnergy[i]*1e6)+'\n');
   }
   writeFile(name+".grain", s, currentWorkingDirectory(), true);
  }
  {array<char> s;
   s.append(str("A, B, Force (N)")+'\n');
   for(auto c: contacts) {
    if(c.a >= grain.base && c.a < grain.base+grain.capacity) {
     if(c.b >= grain.base && c.b < grain.base+grain.capacity) {
      /*auto A = grain.position[c.a-grain.base];
      auto B = grain.position[c.b-grain.base];
      auto rA = c.relativeA;
      auto rB = c.relativeB;
      s.append(str(flat(A), flat(B), flat(rA), flat(rB), flat(c.force))+'\n');*/
      s.append(str(c.a-grain.base, c.b-grain.base, flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".grain-grain", s, currentWorkingDirectory(), true);
  }
  {array<char> s;
   s.append(str("Grain, Side, Force (N)")+'\n');
   for(auto c: contacts) {
    if(c.a >= grain.base && c.a < grain.base+grain.capacity) {
     if(c.b == side.base) {
      /*auto A = grain.position[c.a-grain.base];
      auto B = side.position[c.b-side.base];
      auto rA = c.relativeA;*/
      auto rB = c.relativeB;
      //s.append(str(flat(A), flat(B), flat(rA), flat(rB), flat(c.force))+'\n');
      s.append(str(c.a-grain.base, flat(rB), flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".grain-side", s, currentWorkingDirectory(), true);
  }

  if(wire.count) {array<char> s;
   s.append(str(wire.radius)+'\n');
   s.append(str("P1, P2, grain contact count, grain contact energy (µJ)")+'\n');
   for(size_t i: range(0, wire.count-1)) {
    auto a = wire.position[i], b = wire.position[i+1];
    float energy = 0;
    for(auto& f: wire.frictions[i]) energy += f.energy;
    s.append(str(flat(a), flat(b), wire.frictions[i].size, energy*1e6)+'\n');
   }
   writeFile(name+".wire", s, currentWorkingDirectory(), true);
  }
  if(wire.count) {array<char> s;
   s.append(str("Wire, Grain, Force (N)")+'\n');
   for(auto c: contacts) {
    if(c.a >= wire.base && c.a < wire.base+wire.capacity) {
     if(c.b >= grain.base && c.b < grain.base+grain.capacity) {
      /*auto A = wire.position[c.a-wire.base];
      auto B = grain.position[c.b-grain.base];
      auto rA = c.relativeA;
      auto rB = c.relativeB;*/
      s.append(str(c.a-wire.base, c.b-grain.base, flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".wire-grain", s, currentWorkingDirectory(), true);
  }
  if(wire.count) {array<char> s;
   s.append(str("A, B, Force (N)")+'\n');
   for(auto c: contacts) {
    if(c.a >= wire.base && c.a < wire.base+wire.capacity) {
     if(c.b >= wire.base && c.b < wire.base+wire.capacity) {
      /*auto A = wire.position[c.a-wire.base];
      auto B = wire.position[c.b-wire.base];
      auto rA = c.relativeA;
      auto rB = c.relativeB;*/
      s.append(str(c.a-wire.base, c.b-wire.base, flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".wire-wire", s, currentWorkingDirectory(), true);
  }

  {array<char> s;
   size_t W = side.W;
   for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    vec3 a (toVec3(side.position[i*W+j]));
    vec3 b (toVec3(side.position[i*W+(j+1)%W]));
    vec3 c (toVec3(side.position[(i+1)*W+(j+i%2)%W]));
    s.append(str(flat(a), flat(c))+'\n');
    s.append(str(flat(b), flat(c))+'\n');
    if(i) s.append(str(flat(a), flat(b))+'\n');
   }
   writeFile(name+".side", s, currentWorkingDirectory(), true);
  }
 }

 void step() {
  stepTime.start();
  // Process
  if(processState == Pour) {
   if(currentPourHeight>=pourHeight) {
    processState++;
    log("Pour->Release", grain.count, wire.count);
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && currentPourHeight >= Grain::radius) for(;;) {
     vec4f p {random()*2-1,random()*2-1, currentPourHeight, 0};
     if(length2(p)>1) continue;
     vec4f newPosition {pourRadius*p[0], pourRadius*p[1], p[2], 0}; // Within cylinder
     // Finds lowest Z (reduce fall/impact energy)
     //newPosition[2] = Grain::radius;
     for(auto p: grain.position.slice(0, grain.count)) {
      float x = length(toVec3(p - newPosition).xy());
      if(x < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius) - sq(x));
       newPosition[2] = ::max(newPosition[2], p[2]+dz);
      }
     }
     if(newPosition[2] > currentPourHeight) break;
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     for(auto p: grain.position.slice(0, grain.count)) {
      if(::length(p - newPosition) < Grain::radius+Grain::radius-0x1p-24)
       error(newPosition, p, log2(abs(::length(p - newPosition) -  2*Grain::radius)));
     }
     Locker lock(this->lock);
     assert_(grain.count < grain.capacity);
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
    currentPourHeight += winchSpeed * dt;
   }
  }
  /*if(processState==Release) {
   if(side.initialRadius < 15*Grain::radius) {
    static float releaseTime = timeStep*dt;
    side.castRadius = side.initialRadius * exp((timeStep*dt-releaseTime)/(1*s));
    //side.castRadius = 15*Grain::radius;
   } else {
    log("Release->Wait");
    processState = Wait;
   }
  }*/
  height = 0;
  for(auto p: grain.position.slice(0, grain.count))
   height = ::max(height, p[2]+Grain::radius);

  if(processState == Wait && grainKineticEnergy / grain.count < 100e-6 /*1µJ/grain*/) {
   processState = ProcessState::Load;
   log("Wait->Load", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
   Simulation::snapshot();
  }
  if(processState == ProcessState::Load) {
   if(!load.height) {
    load.height = initialHeight = height;

    // Strain independent results
    float wireDensity = (wire.count-1)*Wire::volume / ((grain.count)*Grain::volume);
    stream.write(str("Grain count:", grain.count, "Wire count:", wire.count,
          "Wire density (%):", wireDensity*100, "Initial Height (mm):", initialHeight*1e3, "\n"));
    stream.write("Load height (mm), Displacement (mm), Height (mm), Force (N), Stretch (%), "
                "Grain kinetic energy (mJ), Wire kinetic energy (mJ), Normal energy (mJ),"
                "Static energy (mJ), Static energy (mJ), Tension energy (mJ), Bend energy (mJ)\n");
   }
   if(load.height >= 3*Grain::radius) {
    if(0) { // Drives load down between 10-100 mm/s
     // Depends on kinetic energy between 10-100µJ/grain
     const float Emin = 10e-6, Emax = 100e-6;
     float a = clamp(0.f, ((grainKineticEnergy/grain.count)-Emin)/(Emax-Emin), 1.f);
     load.height -= dt * (10*a + 100*(1-a)) * mm/s;
    } else {
     load.height -= dt * 10 * mm/s;
    }
    if(height < load.height) {
     if(lastCollapseReport > timeStep+size_t(1e-2/dt)) {
      lastCollapseReport = timeStep;
      log("Collapse");
     }
     load.height = height;
    }
   }
   else {
    processState = Done;
   }
  }

  // Initialize counters
  normalEnergy=0, staticEnergy=0, tensionEnergy=0, bendEnergy=0;
  load.force[0] = _0f;
  recordContacts = true;
  contacts.clear();

  // Grain
  grainTime.start();

  vec4f size = float4(sqrt(3.)/(2*Grain::radius))*(max-min);
  if(size[0]*size[1]*size[2] > 128*128*128) {  // 64 MB
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
    if(load.height) penalty(grain, a, load, 0);
    /*if(processState <= Release)*/ \
    for(size_t b: range(side.faceCount)) penalty(grain, a, side, b);
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

  wireLatticeTime.start();
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) {
    /*size_t index = wireLattice.index(wire.position[a]);
    if(!(index < wireLattice.indices.size)) { log(wire.position[a], max); continue; }
    wireLattice.indices[index] = 1+a;*/
    wireLattice(wire.position[a]) = 1+a;
   }
  }, 1);
  wireLatticeTime.stop();
  //log(wireLattice.indices.size*2/1024); // 2M

  wireContactTime.start();
  if(wire.count) /*parallel_chunk(wire.count,
                                [this,&grainLattice](uint, size_t start, size_t size)*/ {
   size_t start = 0, size = wire.count;
   //assert(size>1);
   uint16* grainBase = grainLattice.indices.begin();
   const int gX = grainLattice.size.x, gY = grainLattice.size.y;
   const uint16* grainNeighbours[3*3] = {
    grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
    grainBase-gX-1, grainBase-1, grainBase+gX-1,
    grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
   };

   uint16* wireBase = wireLattice.indices.begin();
   const int wX = wireLattice.size.x, wY = wireLattice.size.y;
   /*const uint16* wireNeighbours[3*3] = {
       wireBase-wX*wY-wX-1, wireBase-wX*wY-1, wireBase-wX*wY+wX-1,
       wireBase-wX-1, wireBase-1, wireBase+wX-1,
       wireBase+wX*wY-wX-1, wireBase+wX*wY-1, wireBase+wX*wY+wX-1
      };*/
   /*const int r = int( sqrt(sq(2*Wire::radius)+sq(Wire::internodeLength/2))
                        * wireLattice.scale[0]) + 1;*/
   const int r =  2;
   const int n = r+1+r, N = n*n;
   static unused bool once = ({ log(r,n,N); true; });
   const uint16* wireNeighbours[N];
   for(int z: range(n)) for(int y: range(n))
    wireNeighbours[z*n+y] = wireBase+(z-r)*wY*wX+(y-r)*wX-r;
   bool useLattice = true;

   wire.force[start] = float4(wire.mass) * G;
   { // First iteration
    size_t i = start;
    wire.force[i+1] = float4(wire.mass) * G;
    if(i > 0) {
     if(start > 1 && wire.bendStiffness) {// Previous bending spring on first
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
     if(wire.bendStiffness) { // First bending
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
    for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(load.height) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
     for(size_t n: range(3*3)) {
      v4hi line = loada(grainNeighbours[n] + offset);
      if(line[0]) penalty(wire, i, grain, line[0]-1);
      if(line[1]) penalty(wire, i, grain, line[1]-1);
      if(line[2]) penalty(wire, i, grain, line[2]-1);
     }}
    // Wire - Wire
    if(!useLattice) for(size_t b: range(i)) penalty(wire, i, wire, b);
    else {
     size_t offset = wireLattice.index(wire.position[i]);
     //array<size_t> indices;
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) {
        penalty(wire, i, wire, index-1);
        // indices.append(index-1);
       }
      }
     }
     /*for(size_t b: range(i-1)) if(penalty(wire, i, wire, b))
      assert_(indices.contains(b), i, b, indices);*/
    }
   }
   for(size_t i: range(start+1, start+size-1)) {
    // Gravity
    wire.force[i+1] = float4(wire.mass) * G;
    // Tension
    vec4f fT = tension(i-1, i);
    wire.force[i-1] += fT;
    wire.force[i] -= fT;
     if(wire.bendStiffness) { // Bending resistance springs
     vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     vec4f a = C-B, b = B-A;
     vec4f c = cross(a, b);
     vec4f length4 = sqrt(sq3(c));
     float length = length4[0];
     if(length) {
      float angle = atan(length, dot3(a, b)[0]);
      bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
      vec4f p = float3(wire.bendStiffness * angle);
      vec4f dap = cross(a, cross(a,b)) / (sq3(a) * length4);
      vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * length4);
      wire.force[i+1] += p * (-dap);
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
    for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(load.height) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    // Wire - Wire
    if(!useLattice) for(size_t b: range(i)) penalty(wire, i, wire, b);
    else {
     size_t offset = wireLattice.index(wire.position[i]);
     array<uint16> indices;
     array<uint> offsets;
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       int aoffset = wireNeighbours[yz]-wireBase;
       assert_(aoffset+offset+x >= 0, aoffset, wire.position[i], wireLattice.min);
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) {
        offsets.append(aoffset+offset+x);
        if(indices.contains(index-1)) error(offsets, indices, index-1, i, wire.position[i], offset, yz, x, aoffset, wX, wY);
        penalty(wire, i, wire, index-1);
        indices.append(index-1);
       }
      }
     }
     //for(size_t b: range(i-1)) if(penalty(wire, i, wire, b)) assert_(indices.contains(b), b, indices);
    }
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
    // Bending with next chunk
    if(i+1 < wire.count) {
     vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
     vec4f a = C-B, b = B-A;
     vec4f c = cross(a, b);
     vec4f length4 = sqrt(sq3(c));
     float length = length4[0];
     if(length) {
      float angle = atan(length, dot3(a, b)[0]);
      bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
      vec4f p = float3(wire.bendStiffness * angle);
      vec4f dap = cross(a, cross(a,b)) / (sq3(a) * length4);
      vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * length4);
      //wire.force[i+1] += p * (-dap); //-> "Previous bending spring on first"
      wire.force[i] += p * (dap - dbp);
      wire.force[i-1] += p * (dbp);
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
    for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(load.height) penalty(wire, i, load, 0);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    // Wire - Wire
    if(!useLattice) for(size_t b: range(i)) penalty(wire, i, wire, b);
    else {
     size_t offset = wireLattice.index(wire.position[i]);
     //array<size_t> indices;
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) {
        penalty(wire, i, wire, index-1);
        //indices.append(index-1);
       }
      }
     }
     //for(size_t b: range(i-1)) if(penalty(wire, i, wire, b)) assert_(indices.contains(b));
    }
   }

   { // Tension to first node of next chunk
    size_t i = start+size;
    if(i < wire.count) {
     wire.force[i-1] += tension(i-1, i);
      // Bending with first node of next chunk
     if(i+1<wire.count && wire.bendStiffness) {
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f length4 = sqrt(sq3(c));
      float length = length4[0];
      if(length) {
       float angle = atan(length, dot3(a, b)[0]);
       bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
       vec4f p = float3(wire.bendStiffness * angle);
       vec4f dbp = cross(b, cross(b,a)) / (sq3(b) * length4);
       wire.force[i-1] += p * (dbp);
      }
     }
    }
   }
  }//, ::threadCount);
  wireContactTime.stop();

  wireLatticeTime.start();
  // Clears lattice
  parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
   for(size_t a: range(start, start+size)) wireLattice(wire.position[a]) = 0;
  }, 1);
  wireLatticeTime.stop();

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

  {vec4f ssqV = _0f;
   for(vec4f v: grain.velocity.slice(0, grain.count)) ssqV += sq3(v);
   grainKineticEnergy = 1./2*grain.mass*ssqV[0];}
  {vec4f ssqV = _0f;
   for(vec4f v: wire.velocity.slice(0, wire.count)) ssqV += sq3(v);
   wireKineticEnergy = 1./2*wire.mass*ssqV[0];}

  if(processState==ProcessState::Load) {
   /*if(load.force[0][2] > 250 && load.height < 4*Grain::radius)
    processState = Done; // Stops once only two layers are left
   else*/
   if(timeStep%size_t(1e-3/dt) == 0) { // Records results every milliseconds of simulation
    v4sf wireLength = _0f;
    for(size_t i: range(wire.count-1))
     wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
    float stretch = (wireLength[0] / wire.count) / Wire::internodeLength;
    stream.write(str(load.height*1e3, (initialHeight-load.height)*1e3, height*1e3,
                     load.force[0][2], stretch*100, grainKineticEnergy*1e3, wireKineticEnergy*1e3,
      normalEnergy*1e3, staticEnergy*1e3, tensionEnergy*1e3, bendEnergy*1e3)+'\n');
   }
  }

  if(processState==Pour && pattern) { // Generates wire (winch)
   vec2 end;
   if(pattern == Helix) { // Simple helix
    float a = winchAngle;
    float r = winchRadius;
    end = vec2(r*cos(a), r*sin(a));
    winchAngle += winchRate * dt;
   }
   else if(pattern == Cross) { // Cross reinforced helix
    if(currentRadius < -winchRadius) { // Radial -> Tangential (Phase reset)
     currentRadius = winchRadius;
     lastAngle = winchAngle+PI;
     winchAngle = lastAngle;
    }
    if(winchAngle < lastAngle+loopAngle) { // Tangential phase
     float a = winchAngle;
     float r = winchRadius;
     end = vec2(r*cos(a), r*sin(a));
     winchAngle += winchRate * dt;
    } else { // Radial phase
     float a = winchAngle;
     float r = currentRadius;
     end = vec2(r*cos(a), r*sin(a));
     currentRadius -= winchRate*dt * 2*PI*winchRadius; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * loopAngle / (2*PI);
    float R = winchRadius, r = 1./2*R;
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += winchRate * dt;
   } else error(int(pattern));
   float z = currentPourHeight+Grain::radius+Wire::radius;
   vec4f relativePosition = vec3(end, z) - wire.position[wire.count-1];
   vec4f length = sqrt(sq3(relativePosition));
   if(length[0] >= Wire::internodeLength) {
    Locker lock(this->lock);
    assert_(wire.count < wire.capacity);
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

  stepTime.stop();
 }
};
