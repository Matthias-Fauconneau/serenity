#include "system.h"
#include "time.h"
#include <sys/file.h>

struct Grid {
    static constexpr size_t cellCapacity = 8;
    const vec4f scale;
    const vec4f min, max;
    const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
    buffer<uint16> cells {size_t(size.z*size.y*size.x)*cellCapacity};
    const vec4f XYZ0 {float(cellCapacity), float(size.x*cellCapacity), float(size.y*size.x*cellCapacity), 0};
    Grid(float scale, vec4f min, vec4f max) :
      scale(float3(scale)),
      min(min - float3(2./scale)),
      max(max + float3(2./scale)) { // Avoids bound check
     cells.clear(0);
    }
    struct List : mref<uint16> {
        List(mref<uint16> o) : mref(o) {}
        void append(uint16 index) {
         size_t i = 0;
         while(at(i)) i++;
         at(i) = index; i++;
         if(i<cellCapacity) at(i) = 0;
         else assert_(i == cellCapacity);
        }
    };
    inline size_t index(vec4f p) { return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0]; }
    List operator[](size_t i) { return cells.slice(i, cellCapacity); }
    inline List operator()(vec4f p) { return operator[](index(p)); }
};

generic struct Lattice {
 const vec4f scale;
 const vec4f min, max;
 const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
 buffer<T> cells {size_t(size.z*size.y*size.x)};
 const vec4f XYZ0;// {float(1), float(size.x), float(size.y*size.x), 0.f}; // GCC 4.8 \/
 Lattice(float scale, vec4f min, vec4f max) :
   scale(float3(scale)) ,
   min(min - float3(2./scale)),
   max(max + float3(2./scale)), // Avoids bound check
   XYZ0{float(1), float(size.x), float(size.y*size.x), 0.f} {                     // GCC 4.8 ^
  cells.clear(0);
 }
 inline size_t index(vec4f p) { return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0]; }
 inline T& operator()(vec4f p) { return cells[index(p)]; }
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
  assert(p[2] >= min && p[2] < max, min, p, max);
  return int2(int(angle/(2*PI+0x1p-22)*size.x), int((p[2]-min)/(max-min)*size.y));
 }
 inline size_t index(vec4f p) {
  int2 i = index2(p);
  assert(i.x < size.x && i.y < size.y, min, p, max, i, size);
  return i.y*size.x+i.x;
 }
 array<uint16>& operator()(vec4f p) { return cells[index(p)]; }
};

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Pack, Load, Done, Fail };
static string processStates[] {"pour","pack","load","done","failed"};

struct Simulation : System {
 // Process parameters
 sconst bool intersectWireWire = false;
 const float targetHeight;
 const float pourRadius = side.initialRadius - Grain::radius /*- (wire.count?Wire::radius:0)*/;
 const float winchRadius = side.initialRadius /*- Grain::radius*/ - Wire::radius;
 const Pattern pattern;
 const float loopAngle;
 const float initialWinchSpeed = 0.1;
 float winchSpeed = initialWinchSpeed;
 const float targetWireDensity;// = 1./16;
 const float winchRate;
 const float pressure;
 const float plateSpeed;

 // Process variables
 float currentPourHeight = Grain::radius;
 float maxSpawnHeight = 0;
 float lastAngle = 0, winchAngle = 0, currentRadius = winchRadius;
 //float /*currentHeight = 0,*/ initialHeight = 0;
 float topZ0, bottomZ0;
 size_t lastCollapseReport = 0;
 Random random;
 ProcessState processState = Pour;
 bool skip = false;
 size_t packStart;

 // Lattice
 vec4f min = _0f, max = _0f;
 const vec4f wireLatticeMin {-side.initialRadius,-side.initialRadius, -Grain::radius,0.f};
 const vec4f wireLatticeMax { side.initialRadius, side.initialRadius, targetHeight+2*Grain::radius,0.f};
 Lattice<uint32> wireLattice {float(sqrt(3.)/(2*Wire::radius)), wireLatticeMin, wireLatticeMax};

 //float overlapMean = 0, overlapMax = 0;
 float topForce = 0, bottomForce  = 0;
 String debug;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, sideTime;
 Time grainInitializationTime, grainGridTime, grainContactTime, grainIntegrationTime;
 Time wireLatticeTime, wireContactTime, wireIntegrationTime;
 Time sideGridTime, sideForceTime, sideIntegrationTime;

 Lock lock;
 Stream stream;
 Dict parameters;
 String id = str(parameters);
 String name = str(parameters.value("Pattern","none"))+
   (parameters.contains("Pattern")?"-"+str(parameters.value("Rate",0)):""__)+
   (parameters.contains("Pressure")?"-soft"_:"");

 map<rgb3f, array<vec3>> lines;

 Simulation(const Dict& parameters, Stream&& stream) : System(parameters),
   targetHeight(parameters.at("Height"_)),
   pattern(parameters.contains("Pattern")?
                   Pattern(ref<string>(patterns).indexOf(parameters.at("Pattern"))):None),
   loopAngle(parameters.value("Angle"_, PI*(3-sqrt(5.)))),
   //winchSpeed(parameters.value("Speed"_, 0.f)),
   targetWireDensity(parameters.value("Wire"_, 0.f)),
   winchRate(parameters.value("Rate"_, 0.f)),
   pressure(parameters.value("Pressure", 0.f)),
   plateSpeed(parameters.at("PlateSpeed")),
   random((uint)parameters.at("Seed").integer()),
   stream(move(stream)), parameters(copy(parameters)) {
  assert_(pattern == None || targetWireDensity > 0, parameters);
  //this->stream.write("Simulation");
  //log("Simulation");
  //assert(pattern != -1, patterns, this->parameters, this->parameters.at("Pattern"));
  plate.position[1][2] = /*initialHeight =*/ targetHeight+Grain::radius;
  if(pattern) { // Initial wire node
   size_t i = wire.count++;
   wire.position[i] = vec3(winchRadius,0,currentPourHeight);
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
   wire.velocity[i] = _0f;
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
   wire.frictions.set(i);
  }
  //flock(stream.fd, LOCK_EX);
  //float height = plate.position[1][2] - plate.position[0][2];
  //log(volume(), height*PI*sq(side.initialRadius), volume() / (height*PI*sq(side.initialRadius)));
 }

 generic vec4f tension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.Vertex::position[a] - A.Vertex::position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
  A.tensionEnergy += 1./2 * A.tensionStiffness[0] * sq3(x)[0];
  vec4f fS = - A.tensionStiffness * x;
  assert(length[0], a, b);
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = A.Vertex::velocity[a] - A.Vertex::velocity[b];
  vec4f fB = - A.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 // Formats vectors as flat lists without brackets nor commas
 String flat(const vec3& v) { return str(v[0], v[1], v[2]); }
 String flat(const v4sf& v) { return str(v[0], v[1], v[2]); }
 virtual void snapshot() {
  String name = copyRef(this->id); //this->name+"-"+processStates[int(processState)];
  log("Snapshot", name);
  if(existsFile(name+".grain")) remove(name+".grain");
  if(existsFile(name+".wire")) remove(name+".wire");
  if(existsFile(name+".side")) remove(name+".side");
  if(wire.count) {
   float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
   log("Wire density (%):", wireDensity*100);
  }
  Locker lock(this->lock);
  {array<char> s;
   s.append(str(grain.radius)+'\n');
   s.append(str("X, Y, Z, grain contact count, grain contact energy (µJ), "
                              "wire contact count, wire contact energy (µJ)")+'\n');
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
   writeFile(name+".grain.write", s, currentWorkingDirectory(), true);
   rename(name+".grain.write", name+".grain"); // Atomic
  }
  /*{array<char> s;
   s.append(str("A, B, Force (N)")+'\n');
   for(auto c: contacts2) {
    if(c.a >= grain.base && c.a < grain.base+grain.capacity) {
     if(c.b >= grain.base && c.b < grain.base+grain.capacity) {
      s.append(str(c.a-grain.base, c.b-grain.base, flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".grain-grain", s, currentWorkingDirectory(), true);
  }*/
  /*{array<char> s;
   s.append(str("Grain, Side, Force (N)")+'\n');
   for(auto c: contacts2) {
    if(c.a >= grain.base && c.a < grain.base+grain.capacity) {
     if(c.b == side.base) {
      auto rB = c.relativeB;
      s.append(str(c.a-grain.base, flat(rB), flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".grain-side", s, currentWorkingDirectory(), true);
  }*/

  if(wire.count) {array<char> s;
   s.append(str(wire.radius)+'\n');
   s.append(str("P1, P2, grain contact count, grain contact energy (µJ)")+'\n');
   for(size_t i: range(0, wire.count-1)) {
    auto a = wire.position[i], b = wire.position[i+1];
    float energy = 0;
    for(auto& f: wire.frictions[i]) energy += f.energy;
    s.append(str(flat(a), flat(b), wire.frictions[i].size, energy*1e6)+'\n');
   }
   writeFile(name+".wire.write", s, currentWorkingDirectory(), true);
   rename(name+".wire.write", name+".wire"); // Atomic
  }
  /*if(wire.count) {array<char> s;
   s.append(str("Wire, Grain, Force (N)")+'\n');
   for(auto c: contacts2) {
    if(c.a >= wire.base && c.a < wire.base+wire.capacity) {
     if(c.b >= grain.base && c.b < grain.base+grain.capacity) {
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
      s.append(str(c.a-wire.base, c.b-wire.base, flat(c.force))+'\n');
     }
    }
   }
   writeFile(name+".wire-wire", s, currentWorkingDirectory(), true);
  }*/

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
   writeFile(name+".side.write", s, currentWorkingDirectory(), true);
   rename(name+".side.write", name+".side"); // Atomic
  }
 }

 float volume() {
  v4sf O = _0f;
  for(v4sf v : side.Vertex::position) O += v;
  O /= float4(side.Vertex::position.size); // Origin at center of mesh for stability
  v4sf volumeSum = _0f;
  size_t W = side.W;
  for(size_t faceIndex: range(side.faceCount)) {
   size_t i = faceIndex/2/W, j = (faceIndex/2)%W;
   size_t a = i*W+j;
   size_t b = i*W+(j+1)%W;
   size_t c =(i+1)*W+j;
   size_t d = (i+1)*W+(j+1)%W;
   size_t vertices[2][2][3] {{{a,b,c},{b,d,c}},{{a,d,c},{a,b,d}}};
   size_t* V = vertices[i%2][faceIndex%2];
   v4sf A  = side.Vertex::position[V[0]];
   v4sf B  = side.Vertex::position[V[1]];
   v4sf C  = side.Vertex::position[V[2]];
   v4sf volume = ::dot3(A-O, ::cross(B-O, C-O)) / float4(6);
   //assert(volume[0] >= 0, faceIndex, volume[0], A, B, C, O);
   volumeSum += volume;
  }
  float bottom, top;
  { // Bottom fan
   v4sf C = _0f;
   for(v4sf v : side.Vertex::position.slice(0, W)) C += v;
   C /= float4(W); // Origin at center of disk for stability
   bottom = C[2];
   for(size_t i: range(W)) {
    v4sf A  = side.Vertex::position[i];
    v4sf B  = side.Vertex::position[(i+1)%W];
    v4sf volume = - ::dot3(A-O, ::cross(B-O, C-O)) / float4(6);
    //assert_(volume[0] >= 0, volume[0], A, B, C, O);
    volumeSum += volume;
   }
  }
  { // Bottom fan
   v4sf C = _0f;
   for(v4sf v : side.Vertex::position.slice(side.Vertex::position.size-W)) C += v;
   C /= float4(W); // Origin at center of disk for stability
   top = C[2];
   for(size_t i: range(side.Vertex::position.size-W, side.Vertex::position.size)) {
    v4sf A  = side.Vertex::position[i];
    v4sf B  = side.Vertex::position[(i+1)%W];
    v4sf volume = ::dot3(A-O, ::cross(B-O, C-O)) / float4(6);
    //assert_(volume[0] >= 0, volume[0], A, B, C, O);
    volumeSum += volume;
   }
  }
  float plateHeight = plate.position[1][2] - plate.position[0][2];
  //assert_(plateHeight > 0);
  float membraneHeight = top - bottom;
  //assert_(membraneHeight > 0, top, bottom);
  //assert_(membraneHeight >= plateHeight, membraneHeight, plateHeight);
  float volume = volumeSum[0];
  // Corrects for inaccessible volume clipped by plates
  if(membraneHeight > plateHeight) volume -= (membraneHeight - plateHeight) * PI * sq(side.initialRadius);
  // FIXME: Some extra volume between cylinder and membrane remains unclipped
  return volume;
 }

 const float pourPackThreshold = 2e-3;
 const float packLoadThreshold = 2e-3;
 const float transitionTime = 1*s;

 void step() {
  staticFrictionCount2 = staticFrictionCount;
  dynamicFrictionCount2 = dynamicFrictionCount;
  staticFrictionCount = 0; dynamicFrictionCount = 0;

  stepTime.start();
  // Process
  float height = plate.position[1][2] - plate.position[0][2];
  float voidRatio = PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1;
  if(processState == Pour) {
   if(grain.count == grain.capacity ||
      skip || (voidRatio < 0.93 &&
        (!parameters.contains("Pressure") || grainKineticEnergy / grain.count < pourPackThreshold))
      || (/*wire.count &&*/ currentPourHeight >= targetHeight)) {
    skip = false;
    //processState++;
    processState = Pack;
    log("Pour->Pack", grain.count, wire.count,  grainKineticEnergy / grain.count*1e6, voidRatio);
    packStart = timeStep;
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && currentPourHeight >= Grain::radius) for(uint unused t: range(::max(1.,dt/1e-5))) for(;;) {
     vec4f p {random()*2-1,random()*2-1, currentPourHeight, 0};
     if(length2(p)>1) continue;
     vec4f newPosition {pourRadius*p[0], pourRadius*p[1], p[2], 0}; // Within cylinder
     // Finds lowest Z (reduce fall/impact energy)
     for(auto p: grain.position.slice(0, grain.count)) {
      float x = length(toVec3(p - newPosition).xy());
      if(x < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius) - sq(x));
       newPosition[2] = ::max(newPosition[2], p[2]+dz);
      }
     }
     float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
     if(newPosition[2] > currentPourHeight &&
        (wireDensity < targetWireDensity || newPosition[2] > currentPourHeight+2*Grain::radius ||
         newPosition[2] > targetHeight) ) break;
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     maxSpawnHeight = ::max(maxSpawnHeight, newPosition[2]);
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
     grain.rotation[i] = (v4sf){sin(t0)*sin(t1)*sin(t2),
                                    cos(t0)*sin(t1)*sin(t2),
                                    cos(t1)*sin(t2),
                                    cos(t2)};
     // Speeds up pouring
     //if(parameters.contains("Pressure"_))
     grain.velocity[i][2] = - ::min(grain.position[i][2],  0.01f);
     break;
    }
break2_:;
     // Weights winch vertical speed by wire density
    float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
    if(targetWireDensity) { // Adapts winch vertical speed to achieve target wire density
     assert_( wireDensity-targetWireDensity > -1 && wireDensity-targetWireDensity < 1,
              wireDensity, targetWireDensity);
     winchSpeed += dt*/*initialWinchSpeed**/(wireDensity-targetWireDensity);
     //assert_(winchSpeed > 0, winchSpeed, wireDensity, targetWireDensity);
     if(winchSpeed < 0) {
      static bool unused once = ({ log("Initial winch speed too high", initialWinchSpeed,
                                   "for wire density:", str(targetWireDensity)+
                                   ", current wire density:", wireDensity); true; });
      winchSpeed = 0;
     }
    }
    if(currentPourHeight<targetHeight/*-Grain::radius*/)
     currentPourHeight += /*wireDensity **/ winchSpeed * dt;
   }
  }
  float topZ = 0;
  for(auto p: grain.position.slice(0, grain.count))
   topZ = ::max(topZ, p[2]+Grain::radius);
  float alpha = processState == Pack ? ::min(1.f, (timeStep-packStart)*dt / transitionTime) : 1;
  if(processState == Pack) {
   //plate.position[1][2] = ::min(plate.position[1][2], topZ); // Fit plate (Prevent initial decompression)
   if(G[2] < 0) G[2] += dt/transitionTime * gz; else G[2] = 0;
   if(side.thickness > side.loadThickness)
    side.thickness -= dt/transitionTime * (side.pourThickness-side.loadThickness);
   else side.thickness = side.loadThickness;
   side.mass = side.thickness*1e-5*densityScale;
   side.tensionStiffness = float3(side.elasticModulus * side.internodeLength/(2*sqrt(3.)) * side.thickness);
   //side.tensionDamping = float3(side.mass / s);

   float speed = alpha * plateSpeed;
   float dz = dt * speed;
   plate.position[1][2] -= dz;
   plate.position[0][2] += dz;

   if(parameters.contains("Pressure"_)) {
    if(G[2] == 0 && side.thickness == side.loadThickness && (skip || (
                                                              voidRatio < 0.82 &&
                                                              grainKineticEnergy / grain.count < packLoadThreshold &&
                                                              abs(topForce+bottomForce)/(topForce-bottomForce) < 1))) {
     skip = false;
     processState = ProcessState::Load;
     bottomZ0 = plate.position[0][2];
     topZ0 = plate.position[1][2];
     log("Pack->Load", grainKineticEnergy*1e6 / grain.count, "µJ / grain");

     // Strain independent results
     float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
     float height = plate.position[1][2] - plate.position[0][2];
     float grainDensity = grain.count*Grain::volume / (height * PI*sq(side.radius));
     Dict results;
     //results.insert("Initial Radius (mm)"__, side.radius*1e3);
     results.insert("Grain count"__, grain.count);
     results.insert("Weight (N)"__, (grain.count*grain.mass + wire.count*wire.mass) * (-G[2]));
     results.insert("Initial Height (mm)"__, (topZ0-bottomZ0)*1e3);
     results.insert("Grain density (%)"__, grainDensity*100);
     results.insert("Void Ratio (%)"__, voidRatio*100);
     if(wire.count) {
      results.insert("Wire count"__, wire.count);
      results.insert("Wire density (%)"__, wireDensity*100);
     }
     stream.write(str(results)+"\n");
     //Deviatoric Stress (Pa), Normalized Deviatoric Stress,
     stream.write("Strain (%), Stress (Pa), Volume (m³)\n"); //, Tension (J)
    }
   } else {
    if(grainKineticEnergy / grain.count < 1e-6 /*1µJ/grain*/) {
     log("Pack->Done", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
     processState = ProcessState::Done;
     snapshot();
    }
   }
  }
  if(processState == ProcessState::Load) {
   float height = plate.position[1][2]-plate.position[0][2];
   if(height >= 4*Grain::radius && height/(topZ0-bottomZ0) > 1-1./8) {
     float dz = dt * plateSpeed;
     plate.position[1][2] -= dz;
     plate.position[0][2] += dz;
   }
   else {
    log(height >= 4*Grain::radius, height/(topZ0-bottomZ0) > 1-1./8);
    processState = Done;
    snapshot();
   }
  }

  // Energies
  side.tensionEnergy2 = side.tensionEnergy;
  normalEnergy=0, staticEnergy=0, wire.tensionEnergy=0, side.tensionEnergy=0, bendEnergy=0;

  // Plate
  plate.force[0] = _0f;
  plate.force[1] = float4(plate.mass) * G;

  // Soft membrane side
  sideTime.start();
  // Cylinder grid for side
  CylinderGrid vertexGrid {-4*Grain::radius, targetHeight+4*Grain::radius,
     int2(2*PI*side.minRadius/Grain::radius,
          (targetHeight+2*Grain::radius+2*Grain::radius)/Grain::radius)};
  for(size_t i: range(vertexGrid.cells.size)) vertexGrid.cells[i].clear();
  assert_(side.count <= 65536, side.count);
  for(size_t index: range(side.count)) vertexGrid(side.Vertex::position[index]).append(index);
  sideForceTime.start();
  float minRadii[maxThreadCount]; mref<float>(minRadii).clear(inf);
#define DEBUG_TENSION 0
#if DEBUG_TENSION
  Locker lock(this->lock);
  lines.clear();
  float sumForce = 0;
  static float meanForce = 1;
#endif
  parallel_for(1, side.H-1, [this,&minRadii
               #if DEBUG_TENSION
               ,&sumForce
             #endif
               ](uint id, int i) {
   int W = side.W;
   int dy[6] {-W,            0,       W,         W,     0, -W};
   int dx[6] {-1+(i%2), -1, -1+(i%2), (i%2), 1, (i%2)};
   v4sf P = float4(-pressure/(2*3));
   float minRadius = inf;
   for(int j: range(W)) {
    size_t index = i*W+j;
    v4sf O = side.Vertex::position[index];
    minRadius = ::min(minRadius, length2(O));
    // Pressure
    v4sf cross = _0f, tension = _0f;
    for(int e: range(6)) {
     size_t a = i*W+dy[e]+(j+W+dx[e])%W;
     tension += this->tension(side, index, a);
     v4sf A = side.Vertex::position[a];
     v4sf B = side.Vertex::position[i*W+dy[(e+1)%6]+(j+W+dx[(e+1)%6])%W];
     v4sf eA = A-O, eB = B-O;
     cross += ::cross(eB, eA);
    }
    //log(length(cross)/2/6/(sq(side.internodeLength)*sqrt(3.)/4));
    v4sf f = tension + P * cross;
    side.Vertex::force[index] = f; // area = length(cross)/2 / 3vertices
#if DEBUG_TENSION
    sumForce += length(f);
    lines[rgb3f(0,0,1)].append(toVec3(side.Vertex::position[index]));
    lines[rgb3f(0,0,1)].append(toVec3(side.Vertex::position[index]+tension/float4(meanForce/(1*mm))));
    lines[rgb3f(1,0,0)].append(toVec3(side.Vertex::position[index]));
    lines[rgb3f(1,0,0)].append(toVec3(side.Vertex::position[index]+(P * cross)/float4(meanForce/(1*mm))));
    lines[rgb3f(0,1,0)].append(toVec3(side.Vertex::position[index]));
    lines[rgb3f(0,1,0)].append(toVec3(side.Vertex::position[index]+f/float4(meanForce/(1*mm))));
#endif
   }
   minRadii[id] = minRadius;
  },
#if DEBUG_TENSION
  1);
  meanForce = sumForce / (side.W*(side.H-2));
  #else
  threadCount);
#endif
  side.minRadius = ::min(ref<float>(minRadii, threadCount)) - Grain::radius;
  sideForceTime.stop();
  sideTime.stop();

  // Grain
  grainTime.start();

  grainInitializationTime.start();
  vec4f size = float4(sqrt(3.)/(2*Grain::radius))*(max-min);
  if(!(isNumber(min) && isNumber(max)) || size[0]*size[1]*size[2] > 128*128*128) {
   log("Domain", min, max, size); processState = Fail; return;
  }
#define WIRE 1
#if WIRE
  Lattice<uint16> grainLattice(sqrt(3.)/(2*Grain::radius), min, max);
#endif
  Grid grainGrid(1/(2*Grain::radius), min, max);
  grainInitializationTime.stop();

  grainGridTime.start();
  parallel_chunk(grain.count, [this,&grainGrid,
                 #if WIRE
                 &grainLattice,
               #endif
                 &vertexGrid](uint, size_t start, size_t size) {
   for(size_t grainIndex: range(start, start+size)) {
    grain.force[grainIndex] = float4(grain.mass) * G;
    grain.torque[grainIndex] = _0f;
    penalty(grain, grainIndex, plate, 0);
    penalty(grain, grainIndex, plate, 1);
    // Side vertices
    vec4f p = grain.position[grainIndex];
    if(!(p[2] >= vertexGrid.min && p[2] < vertexGrid.max)) {
     log("Bounds", vertexGrid.min, p, vertexGrid.max); processState = Fail; return;
    }
    int2 index = vertexGrid.index2(p);
    for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
     for(int x: range(index.x-1, index.x+1 +1)) {
      // Wraps around (+size.x to wrap -1)
      for(size_t b: vertexGrid.cells[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x])
       penalty(grain, grainIndex, side, b);
     }
    }
#if WIRE
    grainLattice(grain.position[grainIndex]) = 1+grainIndex;
#endif
    grainGrid(grain.position[grainIndex]).append(1+grainIndex);
   }
  }, threadCount);
  if(processState == Fail) return;
  grainGridTime.stop();

  grainContactTime.start();
  parallel_chunk(grainGrid.cells.size/grainGrid.cellCapacity, [this,&grainGrid](uint, size_t start, size_t size) {
   const int Y = grainGrid.size.y, X = grainGrid.size.x;
   const int C = grainGrid.cellCapacity;
   const uint16* chunk = grainGrid.cells.begin() + start*C;
   int di[3*3+3+1];
   size_t i = 0;
   for(int z: range(-1,0 +1)) for(int y: range(-1, (z?1:0) +1)) for(int x: range(-1, ((z||y)?1:-1) +1))
    di[i++] = (z*Y*X + y*X + x)*C;
   for(size_t index: range(size)) {
    const uint16* current = chunk + index*C;
    for(size_t a: range(C)) {
     uint16 A = current[a];
     if(!A) break;
     A--;
     // Same cell
     for(size_t b: range(a)) { // (n-1)n/2
      uint16 B = current[b];
      B--;
      penalty(grain, A, grain, B);
     }
     // Neighbours
     for(size_t n: range(3*3+3+1)) {
      const uint16* neighbour = current + di[n]; // vector
      for(size_t b: range(C)) {
       uint16 B = neighbour[b];
       if(!B) break;
       B--;
       penalty(grain, A, grain, B);
      }
     }
    }
   }
  }, threadCount);
  grainContactTime.stop();
  grainTime.stop();
#if WIRE
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

   uint32* wireBase = wireLattice.cells.begin();
   const int wX = wireLattice.size.x, wY = wireLattice.size.y;
   const int r =  2;
   const int n = r+1+r, N = n*n;
   //static unused bool once = ({ log(r,n,N); true; });
   const uint32* wireNeighbours[N];
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
    penalty(wire, i, plate, 1);
#define WIREBOUNDS 1
#if WIREBOUNDS
    //for(size_t b: range(side.faceCount)) penalty(wire, i, side, b);
    /*// Side vertices
    {int2 index = vertexGrid.index2(wire.position[i]);
    for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
     for(int x: range(index.x-1, index.x+1 +1)) {
      // Wraps around (+size.x to wrap -1)
      for(size_t b: vertexGrid.cells[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x])
       penalty(wire, i, side, b);
     }
    }}*/
    penalty(wire, i, rigidSide, 0);
#endif
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
       uint32 index = wireNeighbours[yz][offset+x];
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
     penalty(wire, i, plate, 1);
#if WIREBOUNDS
    /*// Side vertices
    {int2 index = vertexGrid.index2(wire.position[i]);
    for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
     for(int x: range(index.x-1, index.x+1 +1)) {
      // Wraps around (+size.x to wrap -1)
      for(size_t b: vertexGrid.cells[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x])
       penalty(wire, i, side, b);
     }
    }}*/
    penalty(wire, i, rigidSide, 0);
#endif
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
       uint32 index = wireNeighbours[yz][offset+x];
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
    penalty(wire, i, plate, 1);
#if WIREBOUNDS
    /*// Side vertices
    {int2 index = vertexGrid.index2(wire.position[i]);
    for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
     for(int x: range(index.x-1, index.x+1 +1)) {
      // Wraps around (+size.x to wrap -1)
      for(size_t b: vertexGrid.cells[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x])
       penalty(wire, i, side, b);
     }
    }}*/
    penalty(wire, i, rigidSide, 0);
#endif
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
       uint32 index = wireNeighbours[yz][offset+x];
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
#endif

  // Integration
  min = _0f; max = _0f;

#if WIRE
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
#endif

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

  sideTime.start();
  sideIntegrationTime.start();
  // First and last line are fixed
  parallel_chunk(side.W, side.count-side.W, [this](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) {
    System::step(side, i);
   }
  }, 1);
 sideIntegrationTime.stop();
 sideTime.stop();

  /// All around pressure
 if(processState == ProcessState::Pack) {
  plate.force[0][2] += pressure * PI * sq(side.radius);
  bottomForce = plate.force[0][2];
  //plate.force[0][2] *= (1-alpha); // Transition from force to displacement
  System::step(plate, 0);
  //plate.position[0][2] = ::max(0.f, plate.position[0][2]);
  plate.velocity[0][2] = ::max(0.f, plate.velocity[0][2]); // Only compression

  plate.force[1][2] -= pressure * PI * sq(side.radius);
  topForce = plate.force[1][2];
  //plate.force[1][2] *= (1-alpha); // Transition from force to displacement
  System::step(plate, 1);
  //plate.position[1][2] = ::min(plate.position[1][2], currentHeight);
  plate.velocity[1][2] = ::min(plate.velocity[1][2], 0.f); // Only compression
 } else {
  bottomForce = plate.force[0][2];
  topForce = plate.force[1][2];
 }

  {vec4f ssqV = _0f;
   for(vec4f v: grain.velocity.slice(0, grain.count)) ssqV += sq3(v);
   grainKineticEnergy = 1./2*grain.mass*ssqV[0];}
  {vec4f ssqV = _0f;
   for(vec4f v: wire.velocity.slice(0, wire.count)) ssqV += sq3(v);
   wireKineticEnergy = 1./2*wire.mass*ssqV[0];}

  if(processState==ProcessState::Load) {
   if(size_t((1./60)/dt)==0 || timeStep%size_t((1./60)/dt) == 0) { // Records results
    v4sf wireLength = _0f;
    for(size_t i: range(wire.count-1))
     wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));
    float bottom = plate.force[0][2], top = plate.force[1][2];
    float bottomZ = plate.position[0][2], topZ = plate.position[1][2];
    float displacement = (topZ0-topZ+bottomZ-bottomZ0);
    float stress = (top-bottom)/(2*PI*sq(side.radius));

    String s = str(displacement/(topZ0-bottomZ0)*100, stress, /*stress-pressure,
                   (stress-pressure)/(stress+pressure),*/ volume() /*, side.tensionEnergy*/);
    stream.write(s+'\n');
    // Checks if all grains are within membrane
    // FIXME: actually check only with enlarged rigid cylinder
    for(vec4f p: grain.position.slice(0, grain.count)) {
     if(length2(p) > 2*side.initialRadius) {
      log("Grain slipped through membrane", 2*side.initialRadius, length2(p));
      snapshot();
      processState = ProcessState::Fail;
     }
    }
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
    float R = winchRadius, r = R * loopAngle / (2*PI);
    end = vec2((R-r)*cos(A)+r*cos(a),(R-r)*sin(A)+r*sin(a));
    winchAngle += winchRate*R/r * dt;
   } else error("Unknown pattern:", int(pattern));
   float z = currentPourHeight+Grain::radius+Wire::radius;
   vec4f relativePosition = (v4sf){end[0], end[1], z, 0} - wire.position[wire.count-1];
   vec4f length = sqrt(sq3(relativePosition));
   if(length[0] >= Wire::internodeLength) {
    Locker lock(this->lock);
    assert_(wire.count < wire.capacity, wire.capacity);
    size_t i = wire.count++;
    wire.position[i] = wire.position[wire.count-2]
      + float3(Wire::internodeLength) * relativePosition/length;
    min = ::min(min, wire.position[i]);
    max = ::max(max, wire.position[i]);
    wire.velocity[i] = _0f;
    wire.velocity[i][2] = - ::min(wire.position[i][2],  0.01f);
    for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
    wire.frictions.set(i);
   }
  }

  timeStep++;

  stepTime.stop();
 }

String info() {
 array<char> s {copyRef(processStates[processState])};
 s.append(" "_+str(pressure,0u,1u));
 s.append(" "_+str(dt,0u,1u));
 s.append(" "_+str(grain.count)/*+" grains"_*/);
 s.append(" "_+str(int(timeStep*this->dt/**1e3*/))+"s"_);
 //if(processState==Wait)
 s.append(" "_+decimalPrefix(grainKineticEnergy/*/densityScale*//grain.count, "J"));
 if(processState>=ProcessState::Load) {
  //s.append(" "_+str(int(plate.position[1][2]*1e3))+"mm");
  /*float weight = (grain.count*grain.mass + wire.count*wire.mass) * G[2];
  float stress = (plate.force[1][2]-(plate.force[0][2]-weight))/(2*PI*sq(side.initialRadius));
  s.append(" "_+str(int(stress*1e-6))+"MPa");*/
  float bottomZ = plate.position[0][2], topZ = plate.position[1][2];
  float displacement = (topZ0-topZ+bottomZ-bottomZ0);
  s.append(" "_+str(displacement/(topZ0-bottomZ0)*100));
 }
 if(grain.count) {
  float height = plate.position[1][2] - plate.position[0][2];
  float voidRatio = PI*sq(side.radius)*height / (grain.count*Grain::volume) - 1;
  s.append(" Ratio:"+str(int(voidRatio*100))+"%");
 }
 if(wire.count) {
  float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
  s.append(" Wire density:"+str(int(wireDensity*100))+"%");
 }
 //s.append(" Z:"_+str(int(plate.position[1][2]*1e3)));
 //s.append(" R:"_+str(int(side.radius*1e3)));
 if(processState >= ProcessState::Pack)
  s.append(" "_+str(int((topForce+bottomForce)/(topForce-bottomForce)*100), 2u)+"%");
 //s.append(" Om%"_+str(int(overlapMean/(2*Grain::radius)*100)));
 //s.append(" OM%:"+str(int(overlapMax/(2*Grain::radius)*100)));
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 s.append(" T: "+str(int(side.tensionEnergy2))+"J");
 //if(debug) s.append(" "+debug);
 return move(s);
}
};

