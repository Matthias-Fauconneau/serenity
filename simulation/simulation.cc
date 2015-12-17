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
#if WIRE
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::bendStiffness;
constexpr string Simulation::patterns[];
#endif

Simulation::Simulation(const Dict& p) : System(p.at("TimeStep"), p.at("Radius"))
#if WIRE
  , pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None)
#endif
{
#if WIRE
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight+Grain::radius+Wire::radius;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
  winchAngle += Wire::internodeLength / currentWinchRadius;
 }
#endif
}

void Simulation::step() {
 stepProcess();

 grainTotalTime.start();
 stepGrain();
 grainBottomTotalTime.start();
 stepGrainBottom();
 grainBottomTotalTime.stop();
 if(currentHeight >= topZ) {
  grainTopTotalTime.start();
  stepGrainTop();
  grainTopTotalTime.stop();
 }
 grainGrainTotalTime.start();
 stepGrainGrain();
 grainGrainTotalTime.stop();
 grainTotalTime.stop();

 membraneTotalTime.start();
 if(processState >= ProcessState::Pressure) {
  stepMembrane();
 }
 grainMembraneTotalTime.start();
 stepGrainMembrane();
 grainMembraneTotalTime.stop();
 membraneTotalTime.stop();

 /*stepWire();
 stepGrainWire();
 stepWireTension();
 stepWireBendingResistance();
 stepWireBottom();*/

 grainTotalTime.start();
 stepGrainIntegration();
 validGrainLattice = false;
 grainTotalTime.stop();
 membraneTotalTime.start();
 stepMembraneIntegration();
 membraneTotalTime.stop();
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
 log("W", membrane.W, "H", membrane.H, "W*H", membrane.W*membrane.H, grain.count);
 const bool reset = false;
 size_t accounted = 0, shown = 0;
 map<uint64, String> profile;
#define logTime(name) \
 accounted += name##Time; \
 if(name##Time > stepTime/32) { \
  profile.insertSortedMulti(name ## Time, #name##__); \
  shown += name##Time; \
 } \
 if(reset) name##Time = 0;

 logTime(process);
 logTime(domain);
 logTime(memory);

 profile.insertSortedMulti(grainTotalTime, "=sum{grain}"+
                           strD(grainTime+grainBottomTotalTime+grainTopTotalTime+grainGrainTotalTime
                                +grainIntegrationTime, grainTotalTime));
 if(reset) grainTotalTime.reset();
 logTime(grain);
 profile.insertSortedMulti(grainBottomTotalTime, "=sum{grain-bottom}"_+
                           strD(grainBottomSearchTime+grainBottomRepackFrictionTime+grainBottomFilterTime
                                +grainBottomEvaluateTime+grainBottomSumTime, grainBottomTotalTime));
 if(reset) grainBottomTotalTime.reset();
 logTime(grainBottomSearch);
 logTime(grainBottomFilter);
 logTime(grainBottomRepackFriction);
 logTime(grainBottomEvaluate);
 logTime(grainBottomSum);
 if(grainTopTotalTime) profile.insertSortedMulti(grainTopTotalTime, "=sum{grain-top}"_+
                                                 strD(grainTopSearchTime+grainTopFilterTime+grainTopRepackFrictionTime+grainTopEvaluateTime
                                                      +grainTopSumTime, grainTopTotalTime));
 if(reset) grainTopTotalTime.reset();
 logTime(grainTopSearch);
 logTime(grainTopFilter);
 logTime(grainTopRepackFriction);
 logTime(grainTopEvaluate);
 logTime(grainTopSum);
 profile.insertSortedMulti(grainGrainTotalTime, "=sum{grain-grain}"_+
   strD(grainLatticeTime+grainGrainSearchTime+grainGrainFilterTime+grainGrainRepackFrictionTime
        +grainGrainEvaluateTime+grainGrainSumDomainTime+grainGrainSumAllocateTime+
        grainGrainSumSumTime+grainGrainSumMergeTime, grainGrainTotalTime));
 if(reset) grainGrainTotalTime.reset();
 logTime(grainLattice);
 logTime(grainGrainSearch);
 logTime(grainGrainFilter);
 logTime(grainGrainRepackFriction);
 logTime(grainGrainEvaluate);
 logTime(grainGrainSum);
 logTime(grainGrainSumDomain);
 logTime(grainGrainSumAllocate);
 logTime(grainGrainSumSum);
 logTime(grainGrainSumMerge);

 logTime(grainIntegration);

 profile.insertSortedMulti(grainMembraneTotalTime, "=sum{grain-membrane}"_+
                           strD(grainLatticeTime+grainMembraneSearchTime+grainMembraneFilterTime
                                +grainMembraneRepackFrictionTime+grainMembraneEvaluateTime
                                +grainMembraneSumTime, grainMembraneTotalTime));
 if(reset) membraneTotalTime.reset();
 logTime(membraneInitialization);
 logTime(membraneForce);
 profile.insertSortedMulti(membraneTotalTime, "=sum{membrane}"_+
                           strD(membraneInitializationTime+membraneForceTime
                                +grainMembraneTotalTime+membraneIntegrationTime, membraneTotalTime));
 logTime(grainLattice);
 logTime(grainMembraneSearch);
 logTime(grainMembraneFilter);
 logTime(grainMembraneRepackFriction);
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
  assert(entry.key <= stepTime, entry.value, entry.key/1000000.f, stepTime.cycleCount()/1000000.f);
  log(strD(entry.key, stepTime), entry.value);
 }
 log(strD(shown, stepTime), "/", strD(accounted, stepTime), "/", strD(stepTimeRT, totalTime));
 if(reset) stepTime.reset();
}

/*static inline buffer<int> coreFrequencies() {
 TextData s(File("/proc/cpuinfo"_).readUpToLoop(1<<17));
 assert(s.data.size<s.buffer.capacity, s.data.size, s.buffer.capacity, "/proc/cpuinfo", "coreFrequencies");
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
 for(int unused t: range(int(1/(dt*60))/4)) step();
 stepTime.stop();
 stepTimeRT.stop();
 if(timeStep%(int(1/(dt*60))/4*32) == 0) {
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
