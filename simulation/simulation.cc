// TODO: Factorize force evaluation, contact management
#include "simulation.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-side.h"
//#include "grain-grain.h"
//#include "wire.h"
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

 stepGrainBottom();

 if(processState < ProcessState::Done) {
  grainSideTime.start();
  stepGrainSide();
  grainSideTime.stop();
 }

 stepGrainGrain();

 wireTime.start();
 stepWire();
 wireTime.stop();

 stepGrainWire();

 wireTensionTime.start();
 stepWireTension();
 wireTensionTime.stop();

 wireBendingResistanceTime.start();
 stepWireBendingResistance();
 wireBendingResistanceTime.stop();

 wireBottomTime.start();
 stepWireBottom();
 wireBottomTime.stop();

 grainIntegrationTime.start();
 stepGrainIntegration();
 grainIntegrationTime.stop();

 wireIntegrationTime.start();
 stepWireIntegration();
 wireIntegrationTime.stop();

 timeStep++;
 return true;
}

void Simulation::stepGrain() {
 for(size_t a: range(grain.count)) {
  grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
  grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
 }
}

void Simulation::stepGrainIntegration() {
 float maxGrainV = 0;
 for(size_t i: range(grain.count)) { // TODO: SIMD
  System::step(grain, i);
  maxGrainV = ::max(maxGrainV, length(grain.velocity(i)));
 }
 this->maxGrainV = maxGrainV;
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * dt;
}

#if DEBUG
void Simulation::invariant() {
 for(size_t i: range(wire.count)) {
  assert(isNumber(wire.force(i)) && isNumber(wire.velocity(i)) && isNumber(wire.position(i)));
 }
}
#endif

void Simulation::profile(const Time& totalTime) {
 log("----",timeStep/size_t(64*1/(dt*60)),"----");
 log(totalTime.microseconds()/timeStep, "us/step", totalTime, timeStep);
 if(stepTimeRT.nanoseconds()*100<totalTime.nanoseconds()*99)
  log("step", strD(stepTimeRT, totalTime));
 log("grain-wire contact count mean", grainWireContactSizeSum/timeStep);
 log("grain-wire cycle/grain", (float)grainWireEvaluateTime/grainWireContactSizeSum);
 log("grain-wire B/cycle", (float)(grainWireContactSizeSum*41*4)/grainWireEvaluateTime);
 size_t accounted = 0, shown = 0;
#define logTime(name) \
 accounted += name##Time; \
 if(name##Time > stepTime/16) { \
  log(#name, strD(name ## Time, stepTime)); \
  shown += name##Time; \
 }
 logTime(process);
 logTime(grain);
 logTime(grainBottomFilter);
 logTime(grainBottomEvaluate);
 logTime(grainSide);
 logTime(grainGrainSearch);
 logTime(grainGrainFilter);
 logTime(grainGrainEvaluate);
 logTime(grainGrainSum);
 logTime(grainWireSearch);
 logTime(grainWireFilter);
 logTime(grainWireEvaluate);
 logTime(grainWireSum);
 logTime(grainIntegration);
 logTime(wire);
 logTime(wireTension);
 logTime(wireBendingResistance);
 logTime(wireBottom);
 logTime(wireIntegration);
 log(strD(shown, stepTime), "/", strD(accounted, stepTime));
#undef log
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
 stepTimeRT.start();
 stepTime.start();
 for(int unused t: range(1/(dt*60))) if(!step()) return false;
 stepTime.stop();
 stepTimeRT.stop();
 if(timeStep%(60*size_t(1/(dt*60))) == 0) {
  log(coreFrequencies());
  profile(totalTime);
 }
 return true;
}
