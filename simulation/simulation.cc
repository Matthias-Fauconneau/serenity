// TODO: Factorize force evaluation, contact management
#include "simulation.h"
#include "parallel.h"
//#include "process.h"
//#include "grain.h"
//#include "grain-bottom.h"
//#include "grain-top.h"
//#include "grain-grain.h"
//#include "membrane.h"
//#include "grain-membrane.h"
//include "wire.h"
//include "wire-bottom.h"
//include "grain-wire.h"

constexpr float System::staticFrictionLength;
constexpr float System::Grain::radius;
constexpr float System::Grain::mass;
constexpr float System::Grain::angularMass;
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::bendStiffness;
constexpr string Simulation::patterns[];

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep"), p.at("Radius")),
  pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None) {
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight+Grain::radius+Wire::radius;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
  winchAngle += Wire::internodeLength / currentWinchRadius;
 }
}

void Simulation::step() {
 stepProcess();

 grainTotalTime.start();
 stepGrain();
 stepGrainBottom();
 stepGrainTop();
 stepGrainGrain();
 grainTotalTime.stop();

 if(processState >= ProcessState::Pressure) {
  membraneTotalTime.start();
  stepMembrane();
  membraneTotalTime.stop();
 }
 stepGrainMembrane();

 /*stepWire();
 stepGrainWire();
 stepWireTension();
 stepWireBendingResistance();
 stepWireBottom();*/

 grainTotalTime.start();
 stepGrainIntegration();
 grainTotalTime.stop();
 if(processState >= ProcessState::Pressure) {
  membraneTotalTime.start();
  stepMembraneIntegration();
  membraneTotalTime.stop();
 }
 //stepWireIntegration();

 timeStep++;
}

void Simulation::profile(const Time& totalTime) {
 log("----",timeStep/size_t(1*1/(dt*60)),"----");
 log(totalTime.microseconds()/timeStep, "us/step", totalTime, timeStep);
 //if(stepTimeRT.nanoseconds()*100<totalTime.nanoseconds()*99) log("step", strD(stepTimeRT, totalTime));
#if WIRE
 if(grainWireContactSizeSum) {
  log("grain-wire contact count mean", grainWireContactSizeSum/timeStep);
  log("grain-wire cycle/grain", (float)grainWireEvaluateTime/grainWireContactSizeSum);
  log("grain-wire B/cycle", (float)(grainWireContactSizeSum*41*4)/grainWireEvaluateTime);
 }
#endif
 log("W", membrane.W, "H", membrane.H, "W*H", membrane.W*membrane.H, grain.count, wire.count);
 const bool reset = false;
 size_t accounted = 0, shown = 0;
 map<uint64, string> profile;
#define logTime(name) \
 accounted += name##Time; \
 if(name##Time > stepTime/64) { \
  profile.insertSortedMulti(name ## Time, #name); \
  shown += name##Time; \
 } \
 if(reset) name##Time = 0;
 logTime(process);

 profile.insertSortedMulti(grainTotalTime, "=sum{grain}");
 if(reset) grainTotalTime.reset();
 logTime(grain);
 logTime(grainBottomFilter);
 logTime(grainBottomEvaluate);
 logTime(grainBottomSum);
 logTime(grainSideFilter);
 logTime(grainSideEvaluate);
 logTime(grainSideSum);
 profile.insertSortedMulti(grainGrainTotalTime, "grainGrainTotal");
 if(reset) grainGrainTotalTime.reset();
 logTime(domain);
 logTime(memory);
 logTime(grainGrainLattice);
 logTime(grainGrainSearch);
 logTime(grainGrainFilter);
 logTime(grainGrainEvaluate);
 logTime(grainGrainSumDomain);
 logTime(grainGrainSumZero);
 logTime(grainGrainSumSum);
 logTime(grainGrainSumMerge);

 logTime(grainIntegration);

 profile.insertSortedMulti(membraneTotalTime, "=sum{membrane}");
 if(reset) membraneTotalTime.reset();
 logTime(membraneInitialization);
 logTime(membraneForce);
 logTime(grainMembraneGrid);
 logTime(grainMembraneSearch);
 logTime(grainMembraneFilter);
 logTime(grainMembraneEvaluate);
 logTime(grainMembraneSum);
 logTime(membraneIntegration);

#if WIRE
 logTime(wireInitialization);
 logTime(wireTension);
 logTime(wireBendingResistance);
 logTime(wireBottomFilter);
 logTime(wireBottomEvaluate);
 logTime(wireBottomSum);
 logTime(grainWireSearch);
 logTime(grainWireFilter);
 logTime(grainWireEvaluate);
 logTime(grainWireSum);
 logTime(wireIntegration);
#endif

#undef logTime
 for(const auto entry: profile) {
  assert_(entry.key <= stepTime, entry.value, entry.key/1000000.f, stepTime.cycleCount()/1000000.f);
  log(strD(entry.key, stepTime), entry.value);
 }
 log(strD(shown, stepTime), "/", strD(accounted, stepTime), "/", strD(stepTimeRT, totalTime));
 if(reset) stepTime.reset();
}

/*static inline buffer<int> coreFrequencies() {
 TextData s(File("/proc/cpuinfo"_).readUpToLoop(1<<17));
 assert_(s.data.size<s.buffer.capacity, s.data.size, s.buffer.capacity, "/proc/cpuinfo", "coreFrequencies");
 buffer<int> coreFrequencies (256, 0);
 while(s) {
  if(s.match("cpu MHz"_)) {
   s.until(':'); s.whileAny(' ');
   assert(coreFrequencies.size<coreFrequencies.capacity, s.data);
   coreFrequencies.append( s.decimal() );
  }
  s.line();
 }
 return coreFrequencies;
}*/

bool Simulation::run(const Time& totalTime) {
 stepTimeRT.start();
 stepTime.start();
 for(int unused t: range(1/(dt*60*16))) step();
 stepTime.stop();
 stepTimeRT.stop();
 if(timeStep%(1*size_t(1/(dt*60*16))) == 0) {
  //extern size_t threadCount();
  //if(threadCount()<17) log(threadCount(), coreFrequencies()); else log("//", threadCount());
  profile(totalTime);
 }
 if(processState == Pressure) log("Pressure", int(round(pressure)), "Pa");
 if(processState == Load) {
  float height = topZ-bottomZ;
  float strain = 1-height/topZ0;
  float area = PI*sq(membrane.radius);
  float force = topForceZ + bottomForceZ;
  float stress = force / area;
  log(str(int(round(strain*100)))+"%"_, int(round(stress)), "Pa");
  if(strain > 1./8) return false;
 }
 return true;
}
