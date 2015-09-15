#include "system.h"
#include "time.h"
#include "side.h"

template<size_t N> struct list { // Small sorted list
 struct element { float key=inf; uint value=0; };
 uint size = 0;
 element elements[N];
 bool insert(float key, uint value) {
  int i = 0;
  for(;;) {
   if(i == N) return false;
   if(key<elements[i].key) break;
   i++;
  }
  if(size < N) size++;
  for(int j=size-1; j>i; j--) elements[j]=elements[j-1]; // Shifts right (drop last)
  elements[i] = {key, value}; // Inserts new candidate
  return true;
 }
};

generic struct Lattice {
 const vec4f scale;
 const vec4f min, max;
 const int3 size = int3(::floor(toVec3(scale*(max-min))))+int3(1);
 int YX = size.y * size.x;
 buffer<T> cells {size_t(size.z*size.y*size.x + 3*size.y*size.x+3*size.x+3)}; // -1 .. 2 OOB margins
 T* const base = cells.begin()+size.y*size.x+size.x+1;
 const vec4f XYZ0 {float(1), float(size.x), float(size.y*size.x), 0};
 //Lattice() : scale(_0f), min(_0f), max(_0f) {}
 Lattice(float scale, vec4f min, vec4f max) : scale{scale, scale, scale, 1}, min(min), max(max) {
  cells.clear(0);
 }
 inline size_t index(vec4f O) { return dot3(XYZ0, cvtdq2ps(cvttps2dq(scale*(O-min))))[0]; }
 inline T& cell(vec4f p) { return base[index(p)]; }
 //explicit operator bool() { return cells; }
};

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Pack, Load, Done, Fail };
static string processStates[] {"pour","pack","load","done","failed"};

struct Simulation : System {
 // Process parameters
 sconst bool intersectWireWire = false;
 const float targetHeight;
 const float pourRadius = side.initialRadius - Grain::radius;
 const float winchRadius = side.initialRadius - Wire::radius;
 const Pattern pattern;
 const float loopAngle;
 const float initialWinchSpeed = 0.1;
 float winchSpeed = initialWinchSpeed;
 const float targetWireDensity;
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

 float topForce = 0, bottomForce  = 0;
 float lastBalanceAverage = inf;
 float balanceAverage = 0;
 float balanceSum = 0;
 size_t balanceCount = 0;
 String debug;

 // Performance
 int64 lastReport = realTime(), lastReportStep = timeStep;
 Time totalTime {true}, stepTime;
 Time sideGrainTime, sideForceTime;
 tsc sideGrainTotalTime, sideGrainForceTime;
 Time grainTime, grainGrainTime;
 tsc grainGrainTotalTime, grainGrainForceTime;
 Time integrationTime, processTime;

 Lock lock;
 Stream stream;
 Dict parameters;
 String id = str(parameters);
 String name = str(parameters.value("Pattern","none"))+
   (parameters.contains("Pattern")?"-"+str(parameters.value("Rate",0)):""__)+
   (parameters.contains("Pressure")?"-soft"_:"");

 map<rgb3f, array<vec3>> lines;

 template<Type... Args> void fail(string reason, Args... args) {
  log(reason, args...); processState = Fail; snapshot(reason); return;
 }

 v4sf min = _0f, max = _0f;

 float sideGrainGlobalMinD2 = 0;
 uint sideSkipped = 0;
 buffer<uint16> sideGrainVerletLists {side.capacity * 2};

 float grainGrainGlobalMinD6 = 0;
 uint grainSkipped = 0;
 buffer<uint16> grainGrainVerletLists {grain.capacity * 6};

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
  plate.position[1][2] = targetHeight+Grain::radius;
  if(pattern) { // Initial wire node
   size_t i = wire.count++;
   wire.position[i] = vec3(winchRadius,0,currentPourHeight);
   wire.velocity[i] = _0f;
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
   wire.frictions.set(i);
   // To avoid bound check
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
  }
  /*for(size_t i : range(side.count)) { // Only needed once membrane is soft
   min = ::min(min, side.Vertex::position[i]);
   max = ::max(max, side.Vertex::position[i]);
  }*/
 }

 generic vec4f tension(T& A, size_t a, size_t b) {
  vec4f relativePosition = A.Vertex::position[a] - A.Vertex::position[b];
  vec4f length = sqrt(sq3(relativePosition));
  vec4f x = length - A.internodeLength4;
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
 virtual void snapshot(string step) {
  log("Snapshot disabled"); return;
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
  //Locker lock(this->lock);
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

  if(wire.count) {
   array<char> s;
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

  {
   array<char> s;
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
      || currentPourHeight >= targetHeight) {
    skip = false;
    processState = Pack;
    log("Pour->Pack", grain.count, wire.count,  grainKineticEnergy / grain.count*1e6, voidRatio);
    packStart = timeStep;
    snapshot("pour");
    for(size_t i : range(side.count)) { // Only needed once membrane is soft
     min = ::min(min, side.Vertex::position[i]);
     max = ::max(max, side.Vertex::position[i]);
    }
   } else {
    // Generates falling grain (pour)
    const bool useGrain = 1;
    if(useGrain && currentPourHeight >= Grain::radius) {
     processTime.start();
     for(uint unused t: range(1/*::max(1.,dt/1e-5)*/)) for(;;) {
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
      grain.velocity[i][2] = - ::min(grain.position[i][2],  0.01f);
      break;
     }
     processTime.stop();
    }
break2_:;
    // Weights winch vertical speed by wire density
    float wireDensity = (wire.count-1)*Wire::volume / (grain.count*Grain::volume);
    if(targetWireDensity) { // Adapts winch vertical speed to achieve target wire density
     assert_( wireDensity-targetWireDensity > -1 && wireDensity-targetWireDensity < 1,
              wireDensity, targetWireDensity);
     winchSpeed += dt*/*initialWinchSpeed**/(wireDensity-targetWireDensity);
     if(winchSpeed < 0) {
      static bool unused once = ({ log("Initial winch speed too high", initialWinchSpeed,
                                   "for wire density:", str(targetWireDensity)+
                                   ", current wire density:", wireDensity); true; });
      winchSpeed = 0;
     }
    }
    if(currentPourHeight<targetHeight)
     currentPourHeight += winchSpeed * dt;
   }
  }

  float topZ = 0;
  for(auto p: grain.position.slice(0, grain.count))
   topZ = ::max(topZ, p[2]+Grain::radius);
  float alpha = processState < Pack ? 0 :
                                      (processState == Pack ? ::min(1.f, (timeStep-packStart)*dt / transitionTime) : 1);
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

   float speed = plateSpeed;
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
   }

   if(parameters.contains("Pressure"_)) {
    float plateForce = topForce - bottomForce;
    if((timeStep-packStart)*dt > transitionTime /*&& stress > pressure*/ &&
       (skip ||
        (timeStep-packStart)*dt > 16*s || (
         plateForce / (PI * sq(side.radius)) > pressure &&
         voidRatio < 0.8 &&
         grainKineticEnergy / grain.count < packLoadThreshold &&
         // Not monotonously decreasing anymore
         (balanceAverage < 50 || (lastBalanceAverage < balanceAverage && balanceAverage < 500)) ))) {
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
     stream.write("Radius (m), Height (m), Plate Force (N), Radial Force (N)"
                  );//, Kinetic Energy (J), Contacts\n");
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
  normalEnergy=0, staticEnergy=0;

  // Plate
  plate.force[0] = _0f;
  plate.force[1] = float4(plate.mass) * G;

  // Grain lattice
  grainTime.start();
  float scale = sqrt(3.)/(2*Grain::radius);
  const int3 size = int3(::floor(toVec3(float4(scale)*(max-min))))+int3(1);
  if(size[0] <= 2) max[0]+=1/scale, min[0]-=1/scale;
  if(size[1] <= 2) max[1]+=1/scale, min[1]-=1/scale;
  if(!(isNumber(min) && isNumber(max)) || size.x*size.y*size.z > 128*128*128) {
   return fail("Domain", min, max, size);
  }
  for(size_t grainIndex: range(grain.count)) {
   grain.force[grainIndex] = grain.g;
   grain.torque[grainIndex] = _0f;
   penalty(grain, grainIndex, plate, 0);
   penalty(grain, grainIndex, plate, 1);
   if(processState <= ProcessState::Pour) penalty(grain, grainIndex, rigidSide, 0);
  }
  grainTime.stop();

  unique<Lattice<uint16>> grainLattice = nullptr;

  // Soft membrane side
  //parallel_chunk(1, side.H-1, [this, alpha, &grainLattice](uint, size_t start, size_t size) {
  if(processState > ProcessState::Pour) {
   size_t start = 1, size = side.H-1;
   sideForceTime.start();
   ::side(side.W, side.Vertex::position.data, side.Vertex::force.begin(),
          pressure, side.internodeLength, side.tensionStiffness, side.radius - alpha*Grain::radius,
          start, size);
   sideForceTime.stop();

   sideGrainTime.start();
   sideGrainTotalTime.start();
   if(sideGrainGlobalMinD2 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
    if(!grainLattice) {
     grainLattice = unique<Lattice<uint16>>(sqrt(3.)/(2*Grain::radius), min, max);
     for(size_t grainIndex: range(grain.count))
      grainLattice->cell(grain.position[grainIndex]) = 1+grainIndex; // for grain-grain, and grain-side
    }
    // Side - Grain
    const uint16* grainBase = grainLattice->base;
    int gX = grainLattice->size.x, gY = grainLattice->size.y;
    v4sf scale = grainLattice->scale, min = grainLattice->min;
    const uint16* grainNeighbours[3*3] = {
     grainBase-gX*gY-gX-1, grainBase-gX*gY-1, grainBase-gX*gY+gX-1,
     grainBase-gX-1, grainBase-1, grainBase+gX-1,
     grainBase+gX*gY-gX-1, grainBase+gX*gY-1, grainBase+gX*gY+gX-1
    };
    const v4sf XYZ0 = grainLattice->XYZ0;
    sideGrainGlobalMinD2 = 2*Grain::radius/sqrt(3.) - Grain::radius;
    for(size_t index: range(side.W, side.count-side.W)) {
     v4sf O = side.Vertex::position[index];
     int offset = dot3(XYZ0, floor(scale*(O-min)))[0];
     float minD = sideGrainGlobalMinD2, minD2 = sideGrainGlobalMinD2;
     uint16 first = 0, second = 0;
     for(size_t n=0; n<3*3; n++) { // FIXME: assert unrolled
      v4hi line = *(v4hi*)(offset + grainNeighbours[n]); // Gather
      for(size_t i: range(3)) if(line[i]) { // FIXME: assert unrolled
       float d = contact(side, index, grain, line[i]-1).depth;
       if(d < 0) first = line[i];
       else if(d <= minD2) {
        if(d <= minD) {
         minD = d;
         second = line[i];
        } else {
         minD2 = d;
        }
       }
      }
     }
     assert_(first || !second);
     sideGrainVerletLists[index*2+0] = first;
     sideGrainVerletLists[index*2+1] = second;
     sideGrainGlobalMinD2 = ::min(sideGrainGlobalMinD2, minD2);
    }
    log("side", sideSkipped); sideSkipped=0;
   } else sideSkipped++;
   for(size_t index: range(side.W, side.count-side.W)) {
    for(size_t i: range(2)) {
     int b = sideGrainVerletLists[index*2+i];
     if(!b) break;
     sideGrainForceTime.start();
     penalty(side, index, grain, b-1);
     sideGrainForceTime.stop();
    }
   }
   sideGrainTotalTime.stop();
   sideGrainTime.stop();
  }//,  threadCount);

  /*// Side vertices
  vec4f p = grain.position[grainIndex];
  vec4f radialVector = grain.position[grainIndex]/sqrt(sq2(grain.position[grainIndex]));
  bool sideContact = false;
  int2 index = vertexGrid.index2(p);
  for(int y: range(::max(0, index.y-1), ::min(vertexGrid.size.y, index.y+1 +1))) {
   for(int x: range(index.x-1, index.x+1 +1)) {
    // Wraps around (+size.x to wrap -1)
    for(size_t b: vertexGrid[y*vertexGrid.size.x+(x+vertexGrid.size.x)%vertexGrid.size.x]) {
     vec4f normalForce;
     if(penalty(grain, grainIndex, side, b, normalForce)) {
      sideContact = true;
      float radialForce = dot2(radialVector, -normalForce)[0];
      assert(radialForce >= -0, radialForce, radialVector, normalForce, length2(radialVector));
      radialForceSum += radialForce;
     }
    }
   }
  }
  if(sideContact) {
   sideGrainCount++;
   sideGrainRadiusSum += length2(p);
  }*/

  grainGrainTime.start();
  grainGrainTotalTime.start();
  if(grainGrainGlobalMinD6 <= 0) { // Re-evaluates verlet lists (using a lattice for grains)
   if(!grainLattice) {
    grainLattice = unique<Lattice<uint16>>(sqrt(3.)/(2*Grain::radius), min, max);
    for(size_t grainIndex: range(grain.count))
     grainLattice->cell(grain.position[grainIndex]) = 1+grainIndex; // for grain-grain, and grain-side
   }
   float minD6 = 2*(2*Grain::radius/sqrt(3.)-Grain::radius);
   const int Z = grainLattice->size.z, Y = grainLattice->size.y, X = grainLattice->size.x;
   int di[62];
   size_t i = 0;
   for(int z: range(0, 2 +1)) for(int y: range((z?-2:0), 2 +1)) for(int x: range(((z||y)?-2:1), 2 +1)) {
    di[i++] = z*Y*X + y*X + x;
   }
   assert_(i==62);
   for(size_t index: range(Z*Y*X)) {
    const uint16* current = grainLattice->base + index;
    uint16 A = *current;
    if(!A) continue;
    A--;
    // Neighbours
    list<6> D;
    for(size_t n: range(62)) {
     uint16 B = current[di[n]];
     if(!B) continue;
     B--;
     float d = contact(grain, A, grain, B).depth;
     if(!D.insert(d, B)) minD6 = ::min(minD6, d);
    }
    for(size_t i: range(D.size))
     grainGrainVerletLists[A*6+i] = D.elements[i].value+1;
    for(size_t i: range(D.size, 6))
     grainGrainVerletLists[A*6+i] = 0;
   }
   grainGrainGlobalMinD6 = minD6;
   log("grain", grainSkipped); grainSkipped=0;
  } else grainSkipped++;
  for(size_t index: range(grain.count)) {
   for(size_t i: range(6)) {
    uint16 b = grainGrainVerletLists[index*6+i];
    if(!b) break;
    grainGrainForceTime.start();
    assert_(index != b-1, index, b);
    penalty(grain, index, grain, b-1);
    grainGrainForceTime.stop();
   }
  }
  sideGrainTotalTime.stop();
  grainGrainTime.stop();

  /*// Wire
  wireTime.start();

  wireContactTime.start();
  if(wire.count) parallel_chunk(wire.count,
                                [this,&grainLattice](uint, size_t start, size_t size) {
   assert(size>1);
   uint16* grainBase = grainLattice->cells.begin();
   const int gX = grainLattice->size.x, gY = grainLattice->size.y;
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
    {size_t offset = grainLattice->index(wire.position[i]);
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
    {size_t offset = grainLattice->index(wire.position[i]);
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
    {size_t offset = grainLattice->index(wire.position[i]);
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
  });
  wireContactTime.stop();*/

  // Integration
  min = _0f; max = _0f;

  integrationTime.start();
  for(size_t i: range(wire.count)) {
   System::step(wire, i);
   // To avoid bound check
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
  }
  float maxGrainV = 0;
  for(size_t i: range(grain.count)) {
   System::step(grain, i);
   min = ::min(min, grain.position[i]);
   max = ::max(max, grain.position[i]);
   maxGrainV = ::max(maxGrainV, length(grain.velocity[i]));
  }
  float maxGrainGrainV = maxGrainV + maxGrainV;
  grainGrainGlobalMinD6 -= maxGrainGrainV * dt;
  // First and last line are fixed
  float maxSideV = 0;
  if(processState>=Pack) for(size_t i: range(side.W, side.count-side.W)) {
   System::step(side, i);
   // To avoid bound check
   min = ::min(min, side.Vertex::position[i]);
   max = ::max(max, side.Vertex::position[i]);
   side.Vertex::velocity[i] *= float4(1-10*dt); // Additionnal viscosity
   maxSideV = ::max(maxSideV, length(side.Vertex::velocity[i]));
  }
  float maxSideGrainV = maxGrainV + maxSideV;
  sideGrainGlobalMinD2 -= maxSideGrainV * dt;
  integrationTime.stop();

  /// All around pressure
  processTime.start();
  if(processState == ProcessState::Pack) {
   plate.force[0][2] += pressure * PI * sq(side.radius);
   bottomForce = plate.force[0][2];
   System::step(plate, 0);
   plate.velocity[0][2] = ::max(0.f, plate.velocity[0][2]); // Only compression

   plate.force[1][2] -= pressure * PI * sq(side.radius);
   topForce = plate.force[1][2];
   System::step(plate, 1);
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
    /*assert_(sideGrainCount);
    float radius = sideGrainRadiusSum/sideGrainCount + Grain::radius;
    assert_(isNumber(radius));*/
    float radius = side.radius;
    float radialForceSum = pressure * 2 * PI * radius * height;
    float plateForce = plate.force[1][2] - plate.force[0][2];
    stream.write(str(radius, height, plateForce, radialForceSum/*, grainKineticEnergy, sideGrainCount*/)+'\n');

    // Snapshots every 1% strain
    float strain = (1-height/(topZ0-bottomZ0))*100;
    if(round(strain) > round(lastSnapshotStrain)) {
     lastSnapshotStrain = strain;
     snapshot(str(round(strain))+"%");
    }
    // Checks whether all grains are within membrane
    for(vec4f p: grain.position.slice(0, grain.count)) {
     if(length2(p) > side.maxRadius) {
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
   //Locker lock(this->lock);
   assert_(wire.count < wire.capacity, wire.capacity);
   size_t i = wire.count++;
   v4sf p = wire.position[wire.count-2]
     + float3(Wire::internodeLength) * relativePosition/length;
   wire.position[i] = p;
   min = ::min(min, wire.position[i]);
   max = ::max(max, wire.position[i]);
   wire.velocity[i] = _0f;
   //wire.velocity[i][2] = - ::min(wire.position[i][2],  0.01f);
   for(size_t n: range(3)) wire.positionDerivatives[n][i] = _0f;
   wire.frictions.set(i);
  }
 }
 processTime.stop();
 stepTime.stop();
 timeStep++;
}

String info() {
 array<char> s {copyRef(processStates[processState])};
 s.append(" "_+str(pressure,0u,1u));
 s.append(" "_+str(dt,0u,1u));
 s.append(" "_+str(grain.count)/*+" grains"_*/);
 s.append(" "_+str(int(timeStep*this->dt/**1e3*/))+"s"_);
 s.append(" "_+decimalPrefix(grainKineticEnergy/*/densityScale*//grain.count, "J"));
 if(processState>=ProcessState::Load) {
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
 s.append(" S/D: "+str(staticFrictionCount2, dynamicFrictionCount2));
 if(processState >= ProcessState::Pack) {
  s.append(" "+str(round(lastBalanceAverage), round(balanceAverage)));
  float plateForce = topForce - bottomForce;
  float stress = plateForce/(2*PI*sq(side.initialRadius));
  s.append(" "_+str(decimalPrefix(stress, "Pa"), decimalPrefix(pressure, "Pa")));
 }
 return move(s);
}
};

