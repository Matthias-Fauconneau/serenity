// TODO: Factorize force evaluation, contact management
#include "simulation.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-side.h"
//#include "grain-grain.h"
//#include "grain-wire.h"
//#include "wire-bottom.h"

constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::tensionDamping;

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep")), radius(p.at("Radius")),
  pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight+Grain::radius+Wire::radius;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
  winchAngle += Wire::internodeLength / radius;
 }
}

bool Simulation::domain(vec3& min, vec3& max) {
 min = inf, max = -inf;
 // Grain
 // Avoids bound checks on grain-wire
 for(size_t i: range(grain.count)) {
  min.x = ::min(min.x, grain.Px[i]);
  max.x = ::max(max.x, grain.Px[i]);
  min.y = ::min(min.y, grain.Py[i]);
  max.y = ::max(max.y, grain.Py[i]);
  min.z = ::min(min.z, grain.Pz[i]);
  max.z = ::max(max.z, grain.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain grain", min, max);
  processState = ProcessState::Error;
  return false;
 }
 // Wire
 // Avoids bound checks on grain-wire
 for(size_t i: range(wire.count)) {
  min.x = ::min(min.x, wire.Px[i]);
  max.x = ::max(max.x, wire.Px[i]);
  min.y = ::min(min.y, wire.Py[i]);
  max.y = ::max(max.y, wire.Py[i]);
  min.z = ::min(min.z, wire.Pz[i]);
  max.z = ::max(max.z, wire.Pz[i]);
 }
 if(!(min > vec3(-1) && max < vec3(1))) {
  log("Domain wire", min, max);
  processState = ProcessState::Error;
  return false;
 }
 return true;
}

bool Simulation::step() {
 forces.clear();

 processTime.start();
 stepProcess();
 processTime.stop();

 grainTime.start();
 stepGrain();
 grainTime.stop();
 grainBottomTime.start();
 stepGrainBottom();
 grainBottomTime.stop();
 if(processState == ProcessState::Running) {
  grainSideTime.start();
  stepGrainSide();
  grainSideTime.stop();
 }
 grainGrainTime.start();
 if(!stepGrainGrain()) return false;
 grainGrainTime.stop();
 grainWireTime.start();
 if(!stepGrainWire()) return false;
 grainWireTime.stop();

 wireTime.start();
 stepWire();
 wireTime.stop();
 wireTensionTime.start();
 //stepWireTension();
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
  wire.Fx[i] = 0; wire.Fy[i] = 0; wire.Fz[i] = wire.mass * Gz;
 }
}

void Simulation::stepWireTension() {
 // Tension
 if(wire.count) for(size_t i: range(1, wire.count)) { // TODO: SIMD
  size_t a = i-1, b = i;
  vec3 relativePosition = wire.position(a) - wire.position(b);
  vec3 length = ::length(relativePosition);
  vec3 x = length - vec3(wire.internodeLength);
  vec3 fS = - wire.tensionStiffness * x;
  vec3 direction = relativePosition/length;
  vec3 relativeVelocity = wire.velocity(a) - wire.velocity(b);
  vec3 fB = - wire.tensionDamping * dot(direction, relativeVelocity);
  vec3 fT = (fS + fB) * direction;
  wire.Fx[a] += fT.x;
  wire.Fy[a] += fT.y;
  wire.Fz[a] += fT.z;
  wire.Fx[b] -= fT.x;
  wire.Fy[b] -= fT.y;
  wire.Fz[b] -= fT.z;
 }
}

