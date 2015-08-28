#include "system.h"
#include "time.h"
#include "side.h"
//#include <sys/file.h>

template<size_t cellCapacity> struct List : mref<uint16> {
    List(mref<uint16> o) : mref(o) {}
    void append(uint16 index) {
     size_t i = 0;
     while(at(i)) i++;
     at(i) = index; i++;
     if(i<cellCapacity) at(i) = 0;
     else assert_(i == cellCapacity);
    }
};

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
    inline size_t index(vec4f p) { return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0]; }
    inline List<cellCapacity> operator()(vec4f p) { return cells.slice(index(p), cellCapacity); }
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

#if CYLINDERGRID
struct CylinderGrid {
 const float min, max;
 const int2 size;
 static constexpr size_t cellCapacity = 32;
 buffer<uint16> cells {size_t(size.y*size.x*cellCapacity)};
 const vec2 scale = vec2((size.x-1)/(2*PI /*+0x1p-22*/), (size.y-1)/(max-min));
 CylinderGrid(float min, float max, int2 size) : min(min), max(max), size(size) {
  cells.clear(0);
 }
#if 0
 inline int2 index2(vec4f p) {
     static constexpr float A0 = PI +0x1p-23;
     float angle = A0 + atan(p[1], p[0]);
     int x = int(angle*scale.x);
     int y = (p[2]-min)*scale.y;
     assert_(uint(x) < uint(size.x) && uint(y) < uint(size.y), min, p, max, x, y, size, "index2");
     return int2(x, y);
 }
 inline size_t index(vec4f p) {
     static constexpr float A0 = PI +0x1p-23;
     float angle = A0 + atan(p[1], p[0]);
     int x = int(angle*scale.x);
     int y = (p[2]-min)*scale.y;
     assert_(uint(x) < uint(size.x) && uint(y) < uint(size.y), min, p, max, x, y, size, "index");
     return y*size.x + x;
 }
#else
 inline int2 index2(vec4f p) {
     float angle = clamp(0., PI + atan(p[1], p[0]), 2*PI);
     //return int2(clamp(0, int(angle*scale.x), size.x-1), int((p[2]-min)*scale.y));
     int x = int(angle*scale.x);
     int y = (p[2]-min)*scale.y;
     assert_(uint(x) < uint(size.x) && uint(y) < uint(size.y), min, p, max, x, y, size);
     //return int2(int(angle*scale.x), int((p[2]-min)*scale.y));
     return int2(x, y);
 }
 inline size_t index(vec4f p) {
     //float angle = PI + atan(p[1], p[0]);
     float angle = clamp(0., PI + atan(p[1], p[0]), 2*PI);
     //int x = clamp(0, int(angle*scale.x), size.x-1);
     int x = int(angle*scale.x);
     int y = (p[2]-min)*scale.y;
     assert_(uint(x) < uint(size.x) && uint(y) < uint(size.y), min, p, max, x, y, size);
     return y*size.x + x;
 }
#endif
 //array<uint16>& operator()(vec4f p) { return cells[index(p)]; }
 //inline size_t index(vec4f p) { return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(p-min))))[0]; }
 List<cellCapacity> operator[](size_t i) { return cells.slice(i*cellCapacity, cellCapacity); }
 inline List<cellCapacity> operator()(vec4f p) { return operator[](index(p)); }
};
#endif

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
 float topZ0, bottomZ0;
 float lastSnapshotStrain = 0;
 size_t lastCollapseReport = 0;
 Random random;
 ProcessState processState = Pour;
 bool skip = false;
 size_t packStart;
 size_t lastSnapshot = 0;

 // Lattice
 vec4f min = _0f, max = _0f;
 const vec4f wireLatticeMin {-side.initialRadius*(1+1.f/16),-side.initialRadius*(1+1.f/16), -Grain::radius,0.f};
 const vec4f wireLatticeMax { side.initialRadius*(1+1.f/16), side.initialRadius*(1+1.f/16), targetHeight+2*Grain::radius,0.f};
 Lattice<uint32> wireLattice {float(sqrt(3.)/(2*Wire::radius)), wireLatticeMin, wireLatticeMax};

 //float overlapMean = 0, overlapMax = 0;4
 float topForce = 0, bottomForce  = 0;
 float lastBalanceAverage = inf;
 float balanceAverage = 0;
 float balanceSum = 0;
 size_t balanceCount = 0;
 String debug;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time miscTime, grainTime, wireTime, sideTime;
 Time grainInitializationTime, grainLatticeTime, grainContactTime, grainIntegrationTime;
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
   targetHeight(/*parameters.at("Height"_)*/side.height),
   pattern(parameters.contains("Pattern")?
                   Pattern(ref<string>(patterns).indexOf(parameters.at("Pattern"))):None),
   loopAngle(parameters.value("Angle"_, PI*(3-sqrt(5.)))),
   //winchSpeed(parameters.value("Speed"_, 0.f)),
   targetWireDensity(parameters.value("Wire"_, 0.f)),
   winchRate(parameters.value("Rate"_, 0.f)),
   pressure(parameters.value("Pressure", 0.f)),
   plateSpeed(/*parameters.at("PlateSpeed")*//*parameters.value("PlateSpeed",*/(1e-4)),
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
 }

 generic vec4f tension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.Vertex::position[a] - A.Vertex::position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
  //A.tensionEnergy += 1./2 * A.tensionStiffness[0] * sq3(x)[0];
  vec4f fS = - A.tensionStiffness * x;
  assert(length[0], a, b);
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = A.Vertex::velocity[a] - A.Vertex::velocity[b];
  vec4f fB = - A.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }

 /*generic vec4f expTension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.Vertex::position[a] - A.Vertex::position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
  //A.tensionEnergy += 1./2 * A.tensionStiffness[0] * sq3(x)[0];
  if(x[0] > 0) x = float4(exp( x[0] / A.internodeLength ) - 1); // f(0) = 0, f'(0) = 1
  vec4f fS = - A.tensionStiffness * x;
  assert(length[0], a, b);
  vec4f direction = relativePosition/length;
  vec4f relativeVelocity = A.Vertex::velocity[a] - A.Vertex::velocity[b];
  vec4f fB = - A.tensionDamping * dot3(direction, relativeVelocity);
  return (fS + fB) * direction;
 }*/

 // Formats vectors as flat lists without brackets nor commas
 String flat(const vec3& v) { return str(v[0], v[1], v[2]); }
 String flat(const v4sf& v) { return str(v[0], v[1], v[2]); }
 virtual void snapshot(string step) {
  static int snapshotCount = 0;
  assert_(snapshotCount < 64 /*8 pack + 12 strain + <10 misc (state change)*/);
  snapshotCount++;
  String name = copyRef(this->id)+"."+step;
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

 const float pourPackThreshold = 2e-3;
 const float packLoadThreshold = 500e-6;
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
    /*float maxZ = 0;
    for(auto p: grain.position.slice(0, grain.count)) maxZ = ::max(maxZ, p[2]);
    plate.position[1][2] = ::min(plate.position[1][2], maxZ+Grain::radius);*/
    snapshot("pour");
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && currentPourHeight >= Grain::radius) for(uint unused t: range(::max(1.,dt/1e-5))) for(;;) {
     vec4f p {random()*2-1,random()*2-1, currentPourHeight, 0};
     if(length2(p)>1) continue;
     float r = ::min(pourRadius, side.minRadius);
     vec4f newPosition {r*p[0], r*p[1], p[2], 0}; // Within cylinder
     // Finds lowest Z (reduce fall/impact energy)
     for(auto p: grain.position.slice(0, grain.count)) {
      float x = length(toVec3(p - newPosition).xy());
      if(x < 2*Grain::radius) {
       float dz = sqrt(sq(2*Grain::radius+0x1p-24) - sq(x));
       newPosition[2] = ::max(newPosition[2], p[2]+dz);
      }
     }
     float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
     if(newPosition[2] > currentPourHeight
        // ?
        && (
            wireDensity < targetWireDensity
         || newPosition[2] > currentPourHeight+2*Grain::radius
         || newPosition[2] > targetHeight)
        ) break;
     for(auto p: wire.position.slice(0, wire.count))
      if(length(p - newPosition) < Grain::radius+Wire::radius)
       goto break2_;
     maxSpawnHeight = ::max(maxSpawnHeight, newPosition[2]);
     /*for(auto p: grain.position.slice(0, grain.count)) {
      if(::length(p - newPosition) < Grain::radius+Grain::radius)
       error(newPosition, p, log2(abs(::length(p - newPosition) -  2*Grain::radius)));
     }*/
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
  float alpha = processState < Pack ? 0 : (processState == Pack ? ::min(1.f, (timeStep-packStart)*dt / transitionTime) : 1);
  if(processState == Pack) {
   if(round(timeStep*dt) > round(lastSnapshot*dt)) {
    snapshot(str(uint(round(timeStep*dt)))+"s"_);
    lastSnapshot = timeStep;
   }

   plate.position[1][2] = ::min(plate.position[1][2], topZ); // Fits plate (Prevents initial decompression)
   G[2] = (1-alpha)* -gz/densityScale;
   grain.g = float4(grain.mass) * G;
   side.thickness = (1-alpha) * side.pourThickness + alpha*side.loadThickness;
   side.mass = side.massCoefficient * side.thickness;
   side._1_mass = float3(1./side.mass);
   side.tensionStiffness = side.tensionCoefficient * side.thickness;
   side.tensionDamping = side.mass / s;
   //log(alpha, side.thickness, side.mass, side.tensionStiffness, side.tensionDamping);

   float speed = /*alpha **/ plateSpeed;
   float dz = dt * speed;
   plate.position[1][2] -= dz;
   plate.position[0][2] += dz;

   balanceSum += abs(topForce+bottomForce);
   balanceCount++;
   if(balanceCount > 1./(16*dt)) {
    float average = balanceSum / balanceCount;
    lastBalanceAverage = balanceAverage;
    balanceAverage = average;
    balanceSum = 0;
    balanceCount = 0;
    //log(lastBalanceAverage, balanceAverage);
   }

   if(parameters.contains("Pressure"_)) {
       //float radius = sideGrainRadiusSum/sideGrainCount + Grain::radius;
       /*float plateForce = plate.force[1][2] - plate.force[0][2];
       float stress = plateForce/(2*PI*sq(side.initialRadius));*/

    if((timeStep-packStart)*dt > transitionTime /*&& stress > pressure*/ && (skip ||
                                                    (timeStep-packStart)*dt > 16*s ||
                                                    (
                                                     voidRatio < 0.8 &&
                                                     grainKineticEnergy / grain.count < packLoadThreshold &&
                                                     (balanceAverage < 50 ||
                                                     // Not monotonously decreasing anymore
                                                     (lastBalanceAverage < balanceAverage && balanceAverage < 500))))) {
     //assert_(G[2] == 0 && side.thickness == side.loadThickness);
     skip = false;
     processState = ProcessState::Load;
     bottomZ0 = plate.position[0][2];
     topZ0 = plate.position[1][2];
     log("Pack->Load", grainKineticEnergy*1e6 / grain.count, "µJ / grain", voidRatio);

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
     //stream.write("Strain (%), Stress (Pa), Radius (m), Height (m), Radial Force (N), Tension (J)\n");
     stream.write("Radius (m), Height (m), Plate Force (N), Radial Force (N), Kinetic Energy (J), Contacts\n");
     snapshot("pack");
    }
   } else {
    if(grainKineticEnergy / grain.count < 1e-6 /*1µJ/grain*/) {
     log("Pack->Done", grainKineticEnergy*1e6 / grain.count, "µJ / grain");
     processState = ProcessState::Done;
     snapshot("done");
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
    snapshot("done");
   }
  }

  // Energies
  //side.tensionEnergy2 = side.tensionEnergy;
  normalEnergy=0, staticEnergy=0;
  //wire.tensionEnergy=0, side.tensionEnergy=0, bendEnergy=0;

  // Plate
  plate.force[0] = _0f;
  plate.force[1] = float4(plate.mass) * G;

  // Soft membrane side
  sideTime.start();

  //sideMinTime.start();
  // Cylinder grid for side
  side.minRadius = inf;
  for(size_t index: range(side.count)) {
   v4sf O = side.Vertex::position[index];
   side.minRadius = ::min(side.minRadius, length2(O));
   min = ::min(min, O);
   max = ::max(max, O);
  }
  side.minRadius -= Grain::radius;
  //sideMinTime.stop();

#define GRAINGRID 1
#if CYLINDERGRID
  assert_(side.count <= 65536, side.count);
  CylinderGrid vertexGrid {0, targetHeight,
              int2(2*PI*side.minRadius/(2*Grain::radius), targetHeight/Grain::radius)}; // FIXME

  sideGridTime.start();
  // TODO: multithread SIMD
  for(size_t index: range(side.count)) vertexGrid(side.Vertex::position[index]).append(index);
  sideGridTime.stop();
#endif

#if CYLINDERGRID
  sideForceTime.start();
  parallel_chunk(1, side.H-1, [this, alpha](uint, size_t start, size_t size) {
      ::side(side.W, side.Vertex::position.data, side.Vertex::velocity.data, side.Vertex::force.begin(), pressure,
             side.internodeLength, side.tensionStiffness, side.tensionDamping,
             side.radius - (processState<Pack ? 0 : Grain::radius),
             start, size);
  },  threadCount);
  sideForceTime.stop();
#endif
  sideTime.stop();

  // Grain
  grainTime.start();

  grainInitializationTime.start();
  vec4f size = float4(sqrt(3.)/(2*Grain::radius))*(max-min);
  if(!(isNumber(min) && isNumber(max)) || size[0]*size[1]*size[2] > 128*128*128) {
   log("Domain", min, max, size);
   processState = Fail;
   snapshot("domain");
   return;
  }
  Lattice<uint16> grainLattice(sqrt(3.)/(2*Grain::radius), min, max);
  Grid grainGrid(1/(2*Grain::radius), min, max);
  grainInitializationTime.stop();

  grainLatticeTime.start();
  size_t sideGrainCount = 0; float sideGrainRadiusSum = 0; float radialForce = 0;
  parallel_chunk(grain.count, [this,&grainGrid, &sideGrainCount, &sideGrainRadiusSum, &radialForce,
               #if CYLINDERGRID
                 &vertexGrid,
               #endif
                 &grainLattice
                 ](uint, size_t start, size_t size) {
   float unused sqRadius = sq(side.minRadius);
   for(size_t grainIndex: range(start, start+size)) {
    grain.force[grainIndex] = grain.g;
    grain.torque[grainIndex] = _0f;
    penalty(grain, grainIndex, plate, 0);
    penalty(grain, grainIndex, plate, 1);
#if CYLINDERGRID || !GRAINGRID
    if(0 || sq2(grain.position[grainIndex])[0]/*+Grain::radius*/ >= sqRadius) {
#if CYLINDERGRID
        // Side vertices
        vec4f p = grain.position[grainIndex];
        int2 index = vertexGrid.index2(p);
        bool sideContact = false;
        vec4f radialVector = grain.position[grainIndex]/sqrt(sq3(grain.position[grainIndex]));
        array<size_t> contacts;
        for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
            for(int x: range(index.x-1, index.x+1 +1)) {
                // Wraps around (+size.x to wrap -1)
                for(size_t b: vertexGrid[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x]) {
                    vec4f normalForce;
                    if(penalty(grain, grainIndex, side, b, normalForce)) {
                        contacts.append(b);
                        sideContact = true;
                        radialForce += dot3(radialVector, normalForce)[0];
                    }
                }
            }
        }
        for(size_t b: range(side.count)) {
            if(contact(grain, grainIndex, side, b).depth < 0 && !contacts.contains(b)) {
                error(b, p, index, vertexGrid.cells.indexOf(b), vertexGrid.size);
            }
        }
        if(sideContact) {
            sideGrainCount++;
            sideGrainRadiusSum += length2(p);
        }
#elif !GRAINGRID
        for(size_t b: range(side.count)) penalty(grain, grainIndex, side, b);
#endif
    }
#endif
    grainLattice(grain.position[grainIndex]) = 1+grainIndex;
    grainGrid(grain.position[grainIndex]).append(1+grainIndex);
   }
  }, threadCount);
  if(processState == Fail) return;
  grainLatticeTime.stop();

#if !CYLINDERGRID && GRAINGRID
  grainTime.stop();
  sideTime.start();
  sideForceTime.start();
  parallel_chunk(1, side.H-1, [this, alpha, &grainLattice](uint, size_t start, size_t size) {
      ::side(side.W, side.Vertex::position.data, side.Vertex::velocity.data, side.Vertex::force.begin(), pressure,
             side.internodeLength, side.tensionStiffness, side.tensionDamping,
             side.radius - alpha*Grain::radius,
             start, size);
      // Side - Grain
      uint16* grainBase = grainLattice.cells.begin();
      const int gX = grainLattice.size.x, gY = grainLattice.size.y;
      const uint16* grainNeighbours[3*3] = {
       grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
       grainBase-gX-1, grainBase-1, grainBase+gX-1,
       grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
      };
      int W = side.W;
      for(size_t i=start; i<start+size; i++) { // FIXME -> side
          int base = i*W;
          for(int j=0; j<W; j++) {
              int index = base+j;
              size_t offset = grainLattice.index(side.Vertex::position[index]);
              for(size_t n: range(3*3)) {
                  v4hi line = loada(grainNeighbours[n] + offset);
                  if(line[0]) penalty(side, index, grain, line[0]-1);
                  if(line[1]) penalty(side, index, grain, line[1]-1);
                  if(line[2]) penalty(side, index, grain, line[2]-1);
              }
              //for(size_t b: range(grain.count)) penalty(side, index, grain, b);
          }
      }
  },  threadCount);
  sideForceTime.stop();
  sideTime.stop();
  grainTime.start();
#endif

#if 1
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
#else
  for(size_t a: range(grain.count)) for(size_t b: range(a+1, grain.count))
   penalty(grain, a, grain, b);
#endif
  grainTime.stop();

  // Wire
  wireTime.start();

  if(intersectWireWire) {
      wireLatticeTime.start();
      parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
          for(size_t a: range(start, start+size)) {
              if(wire.position[a][0] < wireLattice.min[0]) wire.position[a][0] = wireLattice.min[0];
              if(wire.position[a][1] < wireLattice.min[1]) wire.position[a][1] = wireLattice.min[1];
              if(wire.position[a][2] < wireLattice.min[2]) wire.position[a][2] = wireLattice.min[2];
              if(wire.position[a][0] > wireLattice.max[0]) wire.position[a][0] = wireLattice.max[0];
              if(wire.position[a][1] > wireLattice.max[1]) wire.position[a][1] = wireLattice.max[1];
              if(wire.position[a][2] > wireLattice.max[2]) wire.position[a][2] = wireLattice.max[2];
              wireLattice(wire.position[a]) = 1+a;
          }
      }, 1);
      wireLatticeTime.stop();
  }

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
      //bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
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
      //bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
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
       //bendEnergy += 1./2 * wire.bendStiffness * sq(angle);
       vec4f p = float3(wire.bendStiffness * angle);
       vec4f dbp = cross(b, c) / (sqrt(sq3(b)) * length4);
       wire.force[i-1] += p * (-dbp);
      }
     }
    }
   }
  }, 1/*::threadCount*/);
  wireContactTime.stop();

  if(intersectWireWire) {
      wireLatticeTime.start();
      // Clears lattice
      parallel_chunk(wire.count, [this](uint, size_t start, size_t size) {
          for(size_t a: range(start, start+size)) wireLattice(wire.position[a]) = 0;
      }, 1);
      wireLatticeTime.stop();
  }

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

  sideTime.start();
  sideIntegrationTime.start();
  // First and last line are fixed
  if(processState>=Pack) parallel_chunk(side.W, side.count-side.W, [this,alpha](uint, size_t start, size_t size) {
   for(size_t i: range(start, start+size)) {
    System::step(side, i);
    /*if(processState <= Pack)*/ {
#if 0
     float l = length2(side.Vertex::position[i]);
     assert(l);
     if(l < side.radius) {
      side.Vertex::position[i][0] *= side.radius/l;
      side.Vertex::position[i][1] *= side.radius/l;
      /*side.Vertex::position[i][0] *= 1+(1-alpha)*(side.radius/l-1);
      side.Vertex::position[i][1] *= 1+(1-alpha)*(side.radius/l-1);*/
     }
#if 0
     else if(l > 2*side.radius) {
      side.Vertex::position[i][0] *= 2*side.radius/l;
      side.Vertex::position[i][1] *= 2*side.radius/l;
      /*side.Vertex::position[i][0] *= 1+(1-alpha)*(side.radius/l-1);
      side.Vertex::position[i][1] *= 1+(1-alpha)*(side.radius/l-1);*/
     }
#endif
#endif
    }
    side.velocity[i] *= float4(1-0.1*dt); // Additionnal viscosity*/
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
    /*v4sf wireLength = _0f;
    for(size_t i: range(wire.count-1))
     wireLength += sqrt(sq3(wire.position[i]-wire.position[i+1]));*/
    //float bottom = plate.force[0][2];
    //float top = plate.force[1][2];
    //float bottomZ = plate.position[0][2];
    //float topZ = plate.position[1][2];
    //float displacement = height0-height;
    //float plateForce = top-bottom;
    //float stress = (top-bottom)/(2*PI*sq(side.radius));

    float radius = sideGrainRadiusSum/sideGrainCount + Grain::radius;
    float plateForce = plate.force[1][2] - plate.force[0][2];
    //float volume = height * PI * sq(radius);
    //if(!initialVolume) initialVolume = volume;
    //(volume/initialVolume-1)*100
    stream.write(str(radius, height, plateForce, radialForce, grainKineticEnergy, sideGrainCount)+'\n'); //, side.tensionEnergy

    // Snapshots every 1% strain
    float strain = (1-height/(topZ0-bottomZ0))*100;
    if(round(strain) > round(lastSnapshotStrain)) {
     lastSnapshotStrain = strain;
     snapshot(str(round(strain))+"%");
    }
    // Checks if all grains are within membrane
    // FIXME: actually check only with enlarged rigid cylinder
    for(vec4f p: grain.position.slice(0, grain.count)) {
     if(length2(p) > 4*side.initialRadius) {
      log("Grain slipped through membrane", 4*side.initialRadius, length2(p));
      snapshot("slip");
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
    v4sf p = wire.position[wire.count-2]
      + float3(Wire::internodeLength) * relativePosition/length;
#if 0
    // Prevents grain intersection
    for(;;) {
     for(auto P: grain.position.slice(0, grain.count)) {
      if(::length(p-P) < (Grain::radius+Wire::radius)*0.99) {
       float r = length2(p-P);
       float z = P[2] + sqrt(sq(Grain::radius+Wire::radius) - sq(r));
       //v4sf np = {p[0],p[1],z,0};
       /*assert_(::length(np-P) > Grain::radius+Wire::radius/2, p-P, r, z-P[2],
         ::length(np-P)/(Grain::radius+Wire::radius/2));*/
       p[2] = z;
       goto break_;
      }
     } /*else*/ break;
     break_:;
    }
#endif
    wire.position[i] = p;
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
 //s.append(" Om%"_+str(int(overlapMean/(2*Grain::radius)*100)));
 //s.append(" OM%:"+str(int(overlapMax/(2*Grain::radius)*100)));
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 //s.append(" T: "+str(int(side.tensionEnergy2))+"J");
 //if(debug) s.append(" "+debug);
 if(processState >= ProcessState::Pack) {
  //s.append(" "_+str(int((topForce+bottomForce)/(topForce-bottomForce)*100), 2u)+"%");
  s.append(" "+str(round(lastBalanceAverage), round(balanceAverage)));
  //s.append(" "_+str(int(abs(topForce+bottomForce))));
  float plateForce = topForce - bottomForce;
  float stress = plateForce/(2*PI*sq(side.initialRadius));
  s.append(" "_+str(decimalPrefix(stress, "Pa"), decimalPrefix(pressure, "Pa")));
 }
 return move(s);
}
};

