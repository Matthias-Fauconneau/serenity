// TODO: Factorize force evaluation, contact management
#include "simulation.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-side.h"
//#include "grain-grain.h"
//#include "grain-wire.h"
//#include "wire-bottom.h"

constexpr float System::staticFrictionLength;
constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::tensionDamping;
constexpr float System::Wire::bendStiffness;
constexpr string Simulation::patterns[];

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep")), radius(p.at("Radius")),
  pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight+Grain::radius+Wire::radius;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
  winchAngle += Wire::internodeLength / radius;
 }
}

void Simulation::domain(vec3& min, vec3& max) {
 min = inf, max = -inf;
 for(size_t i: range(grain.count)) {
  assert(isNumber(grain.Px[i]));
  min.x = ::min(min.x, grain.Px[i]);
  max.x = ::max(max.x, grain.Px[i]);
  assert(isNumber(grain.Py[i]));
  min.y = ::min(min.y, grain.Py[i]);
  max.y = ::max(max.y, grain.Py[i]);
  assert(isNumber(grain.Pz[i]));
  min.z = ::min(min.z, grain.Pz[i]);
  max.z = ::max(max.z, grain.Pz[i]);
 }
 assert_(::max(::max((max-min).x, (max-min).y), (max-min).z) < 32, "grain", min, max);
 for(size_t i: range(wire.count)) {
  min.x = ::min(min.x, wire.Px[i]);
  max.x = ::max(max.x, wire.Px[i]);
  min.y = ::min(min.y, wire.Py[i]);
  max.y = ::max(max.y, wire.Py[i]);
  min.z = ::min(min.z, wire.Pz[i]);
  max.z = ::max(max.z, wire.Pz[i]);
 }
 assert_(::max(::max((max-min).x, (max-min).y), (max-min).z) < 32, "wire", min, max);
}

bool Simulation::step() {
 processTime.start();
 stepProcess();
 processTime.stop();

 grainTime.start();
 stepGrain();
 grainTime.stop();

 wireTime.start();
 stepWire(); // WARNING: Before Grain-Wire force evaluation !!!!!!
 wireTime.stop();

 grainBottomTime.start();
 stepGrainBottom();
 grainBottomTime.stop();
 if(processState < ProcessState::Done) {
  grainSideTime.start();
  stepGrainSide();
  grainSideTime.stop();
 }
 grainGrainTime.start();
 stepGrainGrain();
 grainGrainTime.stop();
 stepGrainWire();
  
 wireTensionTime.start();
 stepWireTension();
 wireTensionTime.stop();
 wireBottomTime.start();
 stepWireBottom();
 wireBottomTime.stop();

 // Grain
 grainTime.start();
 float maxGrainV = 0;
 for(size_t i: range(grain.count)) { // TODO: SIMD
  System::step(grain, i);
  maxGrainV = ::max(maxGrainV, length(grain.velocity(i)));
 }
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * dt;
 grainTime.stop();

 // Wire
 wireTime.start();
 float maxWireV = 0;
 for(size_t i: range(wire.count)) { // TODO: SIMD
  System::step(wire, i);
  maxWireV = ::max(maxWireV, length(wire.velocity(i)));
 }
 float maxGrainWireV = maxGrainV + maxWireV;
 grainWireGlobalMinD -= maxGrainWireV * dt;
 wireTime.stop();

 timeStep++;
 return true;
}

void Simulation::stepGrain() {
 for(size_t a: range(grain.count)) {
  grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
  grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
 }
}

void Simulation::stepWire() {
 for(size_t i: range(wire.count)) { // TODO: SIMD
  wire.Fx[i] = 0; wire.Fy[i] = 0; wire.Fz[i] = Wire::mass * Gz;
 }
 // Bend stiffness
 if(Wire::bendStiffness) for(size_t i: range(1, wire.count-1)) { // TODO: SIMD
  // Bending resistance springs
  vec3 A = wire.position(i-1), B = wire.position(i), C = wire.position(i+1);
  vec3 a = C-B, b = B-A;
  vec3 c = cross(a, b);
  float length = ::length(c);
  if(length) {
   float angle = atan(length, dot(a, b));
   float p = wire.bendStiffness * angle;
   vec3 dap = cross(a, c) / (::length(a) * length);
   vec3 dbp = cross(b, c) / (::length(b) * length);
   wire.Fx[i+1] += p * (-dap).x;
   wire.Fy[i+1] += p * (-dap).y;
   wire.Fz[i+1] += p * (-dap).z;
   wire.Fx[i] += p * (dap + dbp).x;
   wire.Fy[i] += p * (dap + dbp).y;
   wire.Fz[i] += p * (dap + dbp).z;
   wire.Fx[i-1] += p * (-dbp).x;
   wire.Fy[i-1] += p * (-dbp).y;
   wire.Fz[i-1] += p * (-dbp).z;
   if(Wire::bendDamping) {
    vec3 A = wire.velocity(i-1), B = wire.velocity(i), C = wire.velocity(i+1);
    vec3 axis = cross(C-B, B-A);
    float length = ::length(axis);
    if(length) {
     float angularVelocity = atan(length, dot(C-B, B-A));
     vec3 f = (Wire::bendDamping * angularVelocity / 2 / length) * cross(axis, C-A);
     wire.Fx[i] += f.x;
     wire.Fy[i] += f.y;
     wire.Fz[i] += f.z;
    }
   }
  }
 }
}

void Simulation::stepWireTension() {
 if(wire.count == 0) return;
 for(size_t i=0; i<wire.count-1; i+=simd) {
  v8sf Ax = load(wire.Px, i     ), Ay = load(wire.Py, i     ), Az = load(wire.Pz, i    );
#if DEBUG
  v8sf Bx = loadu(wire.Px, i+1), By = loadu(wire.Py, i+1), Bz = loadu(wire.Pz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  v8sf Bx = load(wire.Px, i+1), By = load(wire.Py, i+1), Bz = load(wire.Pz, i+1);
#endif
  v8sf Rx = Ax-Bx, Ry = Ay-By, Rz = Az-Bz;
  v8sf L = sqrt8(Rx*Rx + Ry*Ry + Rz*Rz);
  v8sf x = L - float8(wire.internodeLength);
  v8sf fS = - float8(wire.tensionStiffness) * x;
  v8sf Nx = Rx/L, Ny = Ry/L, Nz = Rz/L;
  v8sf AVx = load(wire.Vx, i     ), AVy = load(wire.Vy, i     ), AVz = load(wire.Vz, i    );
#if DEBUG
  v8sf BVx = loadu(wire.Vx, i+1), BVy = loadu(wire.Vy, i+1), BVz = loadu(wire.Vz, i+1);
#else // CHECKME: How is the unaligned load optimized ?
  v8sf BVx = load(wire.Vx, i+1), BVy = load(wire.Vy, i+1), BVz = load(wire.Vz, i+1);
#endif
  v8sf RVx = AVx - BVx, RVy = AVy - BVy, RVz = AVz - BVz;
  v8sf fB = - float8(wire.tensionDamping) * (Nx * RVx + Ny * RVy + Nz * RVz);
  v8sf f = fS + fB;
  if(i+simd >= wire.count-1) { // Masks invalid force updates
   for(size_t k=wire.count-1-i; k<simd; k++) f[k] = 0; // FIXME
  }
  v8sf FTx = f * Nx;
  v8sf FTy = f * Ny;
  v8sf FTz = f * Nz;

  store(wire.Fx, i, load(wire.Fx, i) + FTx);
  store(wire.Fy, i, load(wire.Fy, i) + FTy);
  store(wire.Fz, i, load(wire.Fz, i) + FTz);
  // FIXME: parallel
  storeu(wire.Fx, i+1, loadu(wire.Fx, i+1) - FTx);
  storeu(wire.Fy, i+1, loadu(wire.Fy, i+1) - FTy);
  storeu(wire.Fz, i+1, loadu(wire.Fz, i+1) - FTz);
 }
}

void Simulation::profile(const Time& totalTime) {
 log("----",timeStep/size_t(64*1/(dt*60)),"----");
 log(totalTime.microseconds()/timeStep, "us/step", totalTime, timeStep);
 if(stepTime.nanoseconds()*100<totalTime.nanoseconds()*99)
  log("step", strD(stepTime, totalTime));
 //log(" process", strD(processTime, stepTimeTSC));
 //log(" grain", strD(grainTime, stepTimeTSC));
 log(" grain-bottom", strD(grainBottomTime, stepTimeTSC));
 //log(" grain-side", strD(grainSideTime, stepTimeTSC));
 log(" grain-grain", strD(grainGrainTime, stepTimeTSC));
 //log(" grain-wire search", strD(grainWireSearchTime, stepTimeTSC));
 //log(" grain-wire filter", strD(grainWireFilterTime, stepTimeTSC));
 log(" grain-wire evaluate", strD(grainWireEvaluateTime, stepTimeTSC.cycleCount()));
 log("grain wire contact count average", grainWireContactSizeSum/timeStep);
 log(" grain-wire evaluate cycle/grain", (float)grainWireEvaluateTime/grainWireContactSizeSum);
 error(" grain-wire evaluate b/cycle", (float)(grainWireContactSizeSum*35*4*8)/grainWireEvaluateTime);
 //log(" grain-wire sum", strD(grainWireSumTime, stepTimeTSC));
 //log(" wire", strD(wireTime, stepTimeTSC));
 //log(" wire-tension", strD(wireTensionTime, stepTimeTSC));
 //log(" wire-bottom", strD(wireBottomTime, stepTimeTSC));
}

static inline buffer<int> coreFrequencies() {
 TextData s(File("/proc/cpuinfo").readUpToLoop(1<<16));
 assert_(s.data.size<s.buffer.capacity);
 buffer<int> coreFrequencies (12, 0);
 while(s) {
  if(s.match("cpu MHz"_)) {
   s.until(':'); s.whileAny(' ');
   coreFrequencies.append( s.decimal() );
  }
  s.line();
 }
 return coreFrequencies;
}

bool Simulation::stepProfile(const Time& totalTime) {
 stepTime.start();
 stepTimeTSC.start();
 for(int unused t: range(1/(dt*60))) if(!step()) return false;
 stepTimeTSC.stop();
 stepTime.stop();
 log(coreFrequencies());
 if(timeStep%(60*size_t(1/(dt*60))) == 0) profile(totalTime);
 return true;
}
