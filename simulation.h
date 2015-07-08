#include "system.h"
#include "time.h"

struct Lattice {
 const vec4f scale;
 const vec4f min, max;
 const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
 buffer<uint16> cells {size_t(size.z*size.y*size.x)};
 const vec4f XYZ0 {float(1), float(size.x), float(size.y*size.x), 0};
 Lattice(float scale, vec4f min, vec4f max) :
   scale(float3(scale)),
   /*min(min - float3(4./scale)),
   max(max + float3(4./scale))*/
   min(min - float3(2./scale)),
   max(max + float3(2./scale)) { // Avoids bound check
  cells.clear(0);
 }
 inline size_t index(vec4f p) {
  return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0];
 }
 inline uint16& operator()(vec4f p) {
  return cells[index(p)];
 }
};

struct CylinderGrid {
 const float min, max;
 const int2 size;
 buffer<array<uint16>> cells {size_t(size.y*size.x)};
 CylinderGrid(float min, float max, int2 size) : min(min), max(max), size(size) {
  cells.clear();
 }
 inline int2 index2(vec4f p) {
  float angle = PI+0x1p-23+atan(p[1], p[0]);
  assert(angle >= 0 && angle/(2*PI+0x1p-22)*size.x < size.x, angle, log2(angle-2*PI));
  assert_(p[2] >= min && p[2] < max, min, p, max);
  return int2(int(angle/(2*PI+0x1p-22)*size.x), int((p[2]-min)/(max-min)*size.y));
 }
 inline size_t index(vec4f p) {
  int2 i = index2(p);
  assert_(i.x < size.x && i.y < size.y, min, p, max, i, size);
  return i.y*size.x+i.x;
 }
 array<uint16>& operator()(vec4f p) { return cells[index(p)]; }
};

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Pack, Load, Done, Fail };
static string processStates[] {"pour","pack","load","done"};

struct Simulation : System {
 // Process parameters
 sconst bool intersectWireWire = false;
 const float targetHeight;
 const float pourRadius = side.initialRadius - Grain::radius - Wire::radius;
 const float winchRadius = side.initialRadius - Grain::radius - Wire::radius;
 const float loopAngle = PI*(3-sqrt(5.));
 const float winchSpeed;
 const float winchRate;
 const Pattern pattern;
 const float pressure;
 const float plateSpeed;

 // Process variables
 float currentPourHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = winchRadius;
 float currentHeight = 0, initialHeight = 0;
 size_t lastCollapseReport = 0;
 Random random;
 ProcessState processState = Pour;
 bool skip = false;

 // Lattice
 vec4f min = _0f, max = _0f;
 Lattice wireLattice{float(sqrt(3.)/(2*Wire::radius)),
    v4sf{-side.initialRadius,-side.initialRadius, -Grain::radius,0},
    v4sf{ side.initialRadius, side.initialRadius, targetHeight+2*Grain::radius,0}};

 // Cylinder grid
 CylinderGrid vertexGrid {-4*Grain::radius, targetHeight+4*Grain::radius,
    int2(2*PI*side./*min*/initialRadius/Grain::radius,
         (targetHeight+2*Grain::radius+2*Grain::radius)/Grain::radius)};
 /*CylinderGrid faceGrid {-4*Grain::radius, targetHeight+2*Grain::radius,
    int2(2*PI*side.initialRadius/Grain::radius,
         (targetHeight+2*Grain::radius+2*Grain::radius)/Grain::radius)};*/

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, solveTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
 Time wireLatticeTime, wireContactTime, wireIntegrationTime;

 Lock lock;
 Stream stream;
 Dict parameters;
 String name = str(parameters.at("Pattern"))+(parameters.at("Pattern")!="none"?"-"+str(parameters.at("Rate")):""__)+
   (parameters.contains("Pressure")?"-soft"_:"");

 Simulation(const Dict& parameters, Stream&& stream) : System(parameters),
   targetHeight(parameters.at("Height"_)),
   winchSpeed(parameters.at("Speed"_)),
   winchRate(parameters.at("Rate"_)),
   pattern(Pattern(ref<string>(patterns).indexOf(parameters.at("Pattern")))),
   pressure(parameters.value("Pressure", 0)),
   plateSpeed(parameters.at("Plate Speed")),
   stream(move(stream)), parameters(copy(parameters)) {
  plate.position[1][2] = initialHeight = targetHeight+Grain::radius;
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

 generic vec4f tension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.Vertex::position[a] - A.Vertex::position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
  A.tensionEnergy += 1./2 * A.tensionStiffness[0] * sq3(x)[0];
  vec4f fS = - A.tensionStiffness * x;
  assert_(length[0], a, b);
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = A.velocity[a] - A.velocity[b];
  vec4f fB = - A.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 // Formats vectors as flat lists without brackets nor commas
 String flat(const vec3& v) { return str(v[0], v[1], v[2]); }
 String flat(const v4sf& v) { return str(v[0], v[1], v[2]); }
 virtual void snapshot() {
  String name = this->name+"-"+processStates[int(processState)];
  if(wire.count) {
   float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
   log("Wire density (%):", wireDensity*100);
  }
  Locker lock(this->lock);
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
   for(auto c: contacts2) {
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
   for(auto c: contacts2) {
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
   for(auto c: contacts2) {
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
   for(auto c: contacts2) {
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
    vec3 a (toVec3(side.Vertex::position[i*W+j]));
    vec3 b (toVec3(side.Vertex::position[i*W+(j+1)%W]));
    vec3 c (toVec3(side.Vertex::position[(i+1)*W+(j+i%2)%W]));
    s.append(str(flat(a), flat(c))+'\n');
    s.append(str(flat(b), flat(c))+'\n');
    if(i) s.append(str(flat(a), flat(b))+'\n');
   }
   writeFile(name+".side", s, currentWorkingDirectory(), true);
  }
 }

 void step() {
  {Locker lock(this->lock);
   flag2 = move(flag);
  }
  stepTime.start();
  // Process
  if(processState == Pour) {
   if(currentHeight >= targetHeight && (skip || !parameters.contains("Pressure") || grainKineticEnergy / grain.count < 8e-6)) {
    skip = false;
    processState++;
    G = _0f;
    log("Pour->Pack", grain.count, wire.count,  grainKineticEnergy / grain.count*1e6);
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
     if(/*wire.count &&*/ newPosition[2] > currentPourHeight) break; // Do not pour over wire
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
     // Speeds up pouring
     if(parameters.contains("Pressure"_)) grain.velocity[i][2] = - ::min(grain.position[i][2], 2.f);
     break;
    }
break2_:;
    if(currentPourHeight<targetHeight-Grain::radius) currentPourHeight += winchSpeed * dt;
   }
  }
  currentHeight = 0;
  for(auto p: grain.position.slice(0, grain.count))
   currentHeight = ::max(currentHeight, p[2]+Grain::radius);
  if(processState == Pack) {
   plate.position[1][2] = initialHeight = ::min(initialHeight, currentHeight); // Fit plate without loading

   if(parameters.contains("Pressure"_)) {
    if(skip || grainKineticEnergy / grain.count < 8e-6) {
     skip = false;
     processState = ProcessState::Load;
     log("Pack->Load", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
     side.faceCount = (side.H-1)*side.W*2; // Switch from rigid membrane to soft

     // Strain independent results
     float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
     float grainDensity = grain.count*Grain::volume / (currentHeight * PI*sq(side.initialRadius));
     stream.write(str("Grain count:", grain.count, "Wire count:", wire.count,
                      "Weight", (grain.count*grain.mass + wire.count*wire.mass) * (-G[2]),
                  "Initial Height (mm):", initialHeight*1e3,
                  "Grain density (%):", grainDensity*100, "\n")
       +(wire.count?str("Wire density (%):", wireDensity*100):""__));
     /*stream.write("Bottom (mm), Top (mm), Displacement (mm), Height (mm), "
                  "Bottom Force with Weight (N), Weight (N), Bottom Force (N), Top Force (N), Force (N), "
                  "Stress (Pa), "
                  "Stretch (%), "
                 "Grain kinetic energy (mJ), Wire kinetic energy (mJ), Normal energy (mJ),"
                 "Static energy (mJ), Static energy (mJ), Tension energy (mJ), Bend energy (mJ)\n");*/
     //Bottom (N), Top (N),
     stream.write("Displacement (mm), Strain (%), Force (N), Stress (Pa)\n"); // Stretch (%)
     /*plate.position[0][2]*1e3, plate.position[1][2]*1e3, (initialHeight-plate.position[1][2]*1e3)*1e3, currentHeight*1e3,
       plate.force[0][2], plate.force[1][2], plate.force[0][2]-weight, (plate.force[0][2]-(plate.force[0][2]-weight)),
       (plate.force[0][2]-(plate.force[0][2]-weight))/(2*PI*sq(side.initialRadius)),
       stretch*100, grainKineticEnergy*1e3, wireKineticEnergy*1e3,
       normalEnergy*1e3, staticEnergy*1e3, wire.tensionEnergy*1e3, bendEnergy*1e3*/
    }
   } else {
    if(grainKineticEnergy / grain.count < 1e-6 /*1µJ/grain*/) {
     Simulation::snapshot();
     log("Pack->Done", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
     processState = ProcessState::Done;
    }
   }
  }
  if(processState == ProcessState::Load) {
   if(plate.position[1][2]-plate.position[0][2] >= 4*Grain::radius && plate.position[0][2]/initialHeight < 1./8) {
    float dz = dt * plateSpeed;
    plate.position[1][2] -= dz;
    plate.position[0][2] += dz;
    if(currentHeight < plate.position[1][2]) {
     if(lastCollapseReport > timeStep+size_t(1e-2/dt)) {
      lastCollapseReport = timeStep;
      log("Collapse");
     }
     plate.position[1][2] = currentHeight;
    }
   }
   else {
    processState = Done;
   }
  }

  // Initialize counters
  normalEnergy=0, staticEnergy=0, wire.tensionEnergy=0, side.tensionEnergy=0;
  bendEnergy=0;
  plate.force[0] = plate.force[1] = _0f;
  recordContacts = true;
  {Locker lock(this->lock);
   contacts2 = move(contacts);
  }

  if(side.faceCount > 1) {
   // Grid
   for(size_t i: range(vertexGrid.cells.size)) vertexGrid.cells[i].clear(); // FIXME: inefficient
   //for(size_t i: range(faceGrid.cells.size)) faceGrid.cells[i].clear(); // FIXME: inefficient
   assert_(side.count < 32768);
   for(size_t i: range(side.count)) {
    side.Vertex::force[i] = _0f;
    vertexGrid(side.Vertex::position[i]).add(i);
    // Plates contact
    side.Vertex::position[i][2] = clamp(vertexGrid.min, side.Vertex::position[i][2], vertexGrid.max-Grain::radius);
    //penalty(side, i, plate, 0);
    //penalty(side, i, plate, 1);
   }
   for(size_t faceIndex: range(side.faceCount)) {
    size_t W = side.W;
    size_t i = faceIndex/2/W, j = (faceIndex/2)%W;
    size_t a = i*W+j;
    size_t b = i*W+(j+1)%W;
    size_t c =(i+1)*W+j;
    size_t d = (i+1)*W+(j+1)%W;
    size_t vertices[2][2][3] {{{a,b,c},{b,d,c}},{{a,d,c},{a,b,d}}};
    size_t* V = vertices[i%2][faceIndex%2];
    /*// Assumes small triangles
    vec3 v[3];
    for(size_t i: range(3)) {
     v[i] = toVec3(side.Vertex::position[V[i]]);
     faceGrid(v[i]).add(faceIndex);
    }
    faceGrid((v[0]+v[1]+v[2])/3.f).add(faceIndex); // FIXME*/
    // Pressure
    vec3 v[3];
    for(size_t i: range(3)) v[i] = toVec3(side.Vertex::position[V[i]]);
    vec3 n = -cross(v[1]-v[0], v[2]-v[0]);
    float length = ::length(n);
    n /= length;
    float area = length / 2;
    vec3 force = pressure * area / 3 * n;
    side.Vertex::force[V[0]] += force;
    side.Vertex::force[V[1]] += force;
    side.Vertex::force[V[2]] += force;
   }
   // Side tension
   size_t W = side.W;
   for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    size_t a = i*W+j, b = i*W+(j+1)%W, c = (i+1)*W+(j+i%2)%W;
    {vec4f t = tension(side, c, a);
     side.Vertex::force[c] += t;
     side.Vertex::force[a] -= t;}
    {vec4f t = tension(side, b, c);
     side.Vertex::force[b] += t;
     side.Vertex::force[c] -= t;}
    if(i) {vec4f t = tension(side, a, b);
     side.Vertex::force[a] += t;
     side.Vertex::force[b] -= t;}
   }
   // Bending resistance
   if(side.bendStiffness) for(size_t i: range(side.H-1)) for(size_t j: range(W)) {
    size_t a = i*W+j;
    size_t b = i*W+(j+1)%W;
    size_t c = (i+1)*W+j;
    size_t d = (i+1)*W+(j+1)%W;
    size_t e = (i+i%2)*W+(j+2)%W;
    size_t vertices[2][2][4] {{{a,b,c,d},{c,b,d,e}},{{c,a,d,b},{a,b,d,e}}};
    for(size_t n: range(2)) {
     size_t* V = vertices[i%2][n];
     vec3 A = toVec3(side.Vertex::position[V[0]]);
     vec3 B = toVec3(side.Vertex::position[V[1]]);
     vec3 C = toVec3(side.Vertex::position[V[2]]);
     vec3 D = toVec3(side.Vertex::position[V[3]]);
     vec3 n1 = cross(B-A, C-A), n2 = cross(B-C, D-C);
     float angle = (dot(n1, D-A) < 0 ? 1 : -1) * acos(dot(n1, n2));
     //bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
     vec4f p = float3(side.bendStiffness * angle);
     vec4f N1 = n1 / sqrt(sq3(n1));
     vec4f N2 = n2 / sqrt(sq3(n1));
     side.Vertex::force[a] += - p * N1;
     side.Vertex::force[b] += p * (N1 + N2) / float4(2);
     side.Vertex::force[c] += p * (N1 + N2) / float4(2);
     side.Vertex::force[d] += - p * N2;
    }
   }
  }

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
   for(size_t grainIndex: range(start, start+size)) {
    grain.force[grainIndex] = float4(grain.mass) * G;
    grain.torque[grainIndex] = _0f;
    penalty(grain, grainIndex, plate, 0);
    if(plate.position[1][2]) penalty(grain, grainIndex, plate, 1);
    if(side.faceCount > 1) {
     // Faces
     /*{int2 index = faceGrid.index2(grain.position[grainIndex]);
      array<size_t> indices; // FIXME: grid radius: triangle max radius + grain radius
      for(int y: range(::max(0, index.y-1), ::min(faceGrid.size.y, index.y+1 +1))) {
       for(int x: range(index.x-1, index.x+1 +1)) {
        // Wraps around (+size.x to wrap -1)
        for(size_t b: faceGrid.cells[y*faceGrid.size.x+(x+faceGrid.size.x)%faceGrid.size.x])
         if(!indices.contains(b) && penalty(grain, grainIndex, side, b))
          indices.append(b);
       }
      }
     }*/
     // Vertices
     {int2 index = vertexGrid.index2(grain.position[grainIndex]);
      for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
       for(int x: range(index.x-1, index.x+1 +1)) {
        // Wraps around (+size.x to wrap -1)
        for(size_t b: vertexGrid.cells[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x])
         penalty(grain, grainIndex, side, b);
       }
      }
     }
     //for(size_t b: range(side.count)) penalty(grain, grainIndex, side, b);
    } else penalty(grain, grainIndex, side, 0);
    grainLattice(grain.position[grainIndex]) = 1+grainIndex;
   }
  }, 1);
  if(processState == Done) return; // DEBUG
  grainInitializationTime.stop();

  grainContactTime.start();
  parallel_chunk(grainLattice.cells.size,
                 [this,&grainLattice](uint, size_t start, size_t size) {
   const int Y = grainLattice.size.y, X = grainLattice.size.x;
   const uint16* chunk = grainLattice.cells.begin() + start;
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
   for(size_t a: range(start, start+size)) wireLattice(wire.position[a]) = 1+a;
  }, 1);
  wireLatticeTime.stop();

  wireContactTime.start();
  if(wire.count) parallel_chunk(wire.count,
                                [this,&grainLattice](uint, size_t start, size_t size) {
   assert(size>1);
   uint16* grainBase = grainLattice.cells.begin();
   const int gX = grainLattice.size.x, gY = grainLattice.size.y;
   const uint16* grainNeighbours[3*3] = {
    grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
    grainBase-gX-1, grainBase-1, grainBase+gX-1,
    grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
   };

   uint16* wireBase = wireLattice.cells.begin();
   const int wX = wireLattice.size.x, wY = wireLattice.size.y;
   const int r =  2;
   const int n = r+1+r, N = n*n;
   static unused bool once = ({ log(r,n,N); true; });
   const uint16* wireNeighbours[N];
   for(int z: range(n)) for(int y: range(n))
    wireNeighbours[z*n+y] = wireBase+(z-r)*wY*wX+(y-r)*wX-r;

   wire.force[start] = float4(wire.mass) * G;
   { // First iteration
    size_t i = start;
    wire.force[i+1] = float4(wire.mass) * G;
    if(i > 0) {
     if(start > 1 && wire.bendStiffness) { // Previous bending spring on first
      size_t i = start-1;
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f length4 = sqrt(sq3(c));
      if(length4[0]) {
       float p = wire.bendStiffness * atan(length4[0], dot3(a, b)[0]);
       vec4f dap = cross(a, c) / (sqrt(sq3(a)) * length4);
       wire.force[i+1] += float3(p) * (-dap);
      }
     }
     // First tension
     vec4f fT = tension(wire, i-1, i);
     wire.force[i] -= fT;
     if(wire.bendStiffness) { // First bending
      vec4f A = wire.position[i-1], B = wire.position[i], C = wire.position[i+1];
      vec4f a = C-B, b = B-A;
      vec4f c = cross(a, b);
      vec4f length4 = sqrt(sq3(c));
      if(length4[0]) {
       float p = wire.bendStiffness * atan(length4[0], dot3(a, b)[0]);
       vec4f dap = cross(a, c) / (sqrt(sq3(a)) * length4);
       vec4f dbp = cross(b, c) / (sqrt(sq3(b)) * length4);
       wire.force[i+1] += float3(p) * (-dap);
       wire.force[i] += float3(p) * (dap + dbp);
        //wire.force[i-1] += float3(p) * (-dbp); //-> "Tension to first node of next chunk"
       { // Damping
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
    penalty(wire, i, plate, 0);
    //for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(side.faceCount==1) penalty(wire, i, side, 0); // FIXME
    if(plate.position[1][2]) penalty(wire, i, plate, 1);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
     for(size_t n: range(3*3)) {
      v4hi line = loada(grainNeighbours[n] + offset);
      if(line[0]) penalty(wire, i, grain, line[0]-1);
      if(line[1]) penalty(wire, i, grain, line[1]-1);
      if(line[2]) penalty(wire, i, grain, line[2]-1);
     }}
    if(intersectWireWire) {// Wire - Wire
     size_t offset = wireLattice.index(wire.position[i]);
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) penalty(wire, i, wire, index-1);
      }
     }
    }
   }
   for(size_t i: range(start+1, start+size-1)) {
    // Gravity
    wire.force[i+1] = float4(wire.mass) * G;
    // Tension
    vec4f fT = tension(wire, i-1, i);
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
      vec4f dap = cross(a, c) / (sqrt(sq3(a)) * length4);
      vec4f dbp = cross(b, c) / (sqrt(sq3(b)) * length4);
      wire.force[i+1] += p * (-dap);
      wire.force[i] += p * (dap + dbp);
      wire.force[i-1] += p * (-dbp);
      {
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
    penalty(wire, i, plate, 0);
    //for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(side.faceCount==1) penalty(wire, i, side, 0); // FIXME
    if(plate.position[1][2]) penalty(wire, i, plate, 1);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    if(intersectWireWire) { // Wire - Wire
     size_t offset = wireLattice.index(wire.position[i]);
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) penalty(wire, i, wire, index-1);
      }
     }
    }
   }

   // Last iteration
   if(size>1) {
    size_t i = start+size-1;
    // Gravity
    wire.force[i] = float4(wire.mass) * G;
     // Tension
    vec4f fT = tension(wire, i-1, i);
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
      vec4f dap = cross(a, c) / (sqrt(sq3(a)) * length4);
      vec4f dbp = cross(b, c) / (sqrt(sq3(b)) * length4);
      //wire.force[i+1] += p * (-dap); //-> "Previous bending spring on first"
      wire.force[i] += p * (dap + dbp);
      wire.force[i-1] += p * (-dbp);
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
    penalty(wire, i, plate, 0);
    //for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    if(side.faceCount==1) penalty(wire, i, side, 0); // FIXME
    if(plate.position[1][2]) penalty(wire, i, plate, 1);
    // Wire - Grain
    {size_t offset = grainLattice.index(wire.position[i]);
    for(size_t n: range(3*3)) {
     v4hi line = loada(grainNeighbours[n] + offset);
     if(line[0]) penalty(wire, i, grain, line[0]-1);
     if(line[1]) penalty(wire, i, grain, line[1]-1);
     if(line[2]) penalty(wire, i, grain, line[2]-1);
    }}
    if(intersectWireWire) { // Wire - Wire
     size_t offset = wireLattice.index(wire.position[i]);
     for(size_t yz: range(N)) {
      for(size_t x: range(n)) {
       uint16 index = wireNeighbours[yz][offset+x];
       if(index > i+2) penalty(wire, i, wire, index-1);
      }
     }
    }
   }

   { // Tension to first node of next chunk
    size_t i = start+size;
    if(i < wire.count) {
     wire.force[i-1] += tension(wire, i-1, i);
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
       vec4f dbp = cross(b, c) / (sqrt(sq3(b)) * length4);
       wire.force[i-1] += p * (-dbp);
      }
     }
    }
   }
  }, 1/*::threadCount*/);
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

  if(side.faceCount > 1)
   // First and last line are fixed
   parallel_chunk(side.W, side.count-side.W, [this](uint, size_t start, size_t size) {
    for(size_t i: range(start, start+size)) {
     System::step(side, i);
    }
   }, 1);

  {vec4f ssqV = _0f;
   for(vec4f v: grain.velocity.slice(0, grain.count)) ssqV += sq3(v);
   grainKineticEnergy = 1./2*grain.mass*ssqV[0];}
  {vec4f ssqV = _0f;
   for(vec4f v: wire.velocity.slice(0, wire.count)) ssqV += sq3(v);
   wireKineticEnergy = 1./2*wire.mass*ssqV[0];}

  if(processState==ProcessState::Load) {
   if(timeStep%size_t(1e-3/dt) == 0) { // Records results every milliseconds of simulation
    v4sf wireLength = _0f;
    for(size_t i: range(wire.count-1))
     wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
    float weight = (grain.count*grain.mass + wire.count*wire.mass) * G[2];
    float bottom = plate.force[0][2], top = plate.force[1][2];
    float force = (top-(bottom-weight));
    // Displacement (mm), Force (N), Strain (%), Stress (Pa) //, Stretch (%)
    String s = str(plate.position[0][2]*1e3,  plate.position[0][2]/initialHeight*100,
      /*bottom, top,*/ force, force/(2*PI*sq(side.initialRadius)));
    if(wire.count) {
     float stretch = (wireLength[0] / wire.count) / Wire::internodeLength;
     s.append(stretch*100);
    }
    stream.write(s+'\n');
    /*stream.write(str(
      plate.position[0][2]*1e3, plate.position[1][2]*1e3, (initialHeight-plate.position[1][2])*1e3, currentHeight*1e3,
      plate.force[0][2], weight, plate.force[0][2]-weight,  plate.force[1][2], (plate.force[1][2]-(plate.force[0][2]-weight)),
      force/(2*PI*sq(side.initialRadius)),
      stretch*100, grainKineticEnergy*1e3, wireKineticEnergy*1e3,
      normalEnergy*1e3, staticEnergy*1e3, wire.tensionEnergy*1e3, bendEnergy*1e3)+'\n');*/
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
     currentRadius -= winchRate * dt * winchRadius; // Constant velocity
    }
   }
   else if (pattern == Loop) { // Loops
    float A = winchAngle, a = winchAngle * (2*PI) / loopAngle;
    float R = winchRadius, r = 1./2*R;
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += winchRate*R/r * dt;
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
