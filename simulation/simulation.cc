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

void Simulation::domainGrain(vec3& min, vec3& max) {
 float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
 vXsf minX_ = _0f, minY_ = _0f, minZ_ = _0f, maxX_ = _0f, maxY_ = _0f, maxZ_ = _0f;
 for(size_t i=0; i<grain.count; i+=simd) {
  vXsf X = load(Px, i), Y = load(Py, i), Z = load(Pz, i);
  minX_ = ::min(minX_, X);
  maxX_ = ::max(maxX_, X);
  minY_ = ::min(minY_, Y);
  maxY_ = ::max(maxY_, Y);
  minZ_ = ::min(minZ_, Z);
  maxZ_ = ::max(maxZ_, Z);
 }
 const float minX = ::min(minX_);
 const float minY = ::min(minY_);
 const float minZ = ::min(minZ_);
 const float maxX = ::max(maxX_);
 const float maxY = ::max(maxY_);
 const float maxZ = ::max(maxZ_);
 assert_(maxX-minX < 16 && maxY-minY < 16 && maxZ-minZ < 16, "grain",
         maxX-minX, maxY-minY, maxZ-minZ, "\n",
         minX, maxX, minY, maxY, minZ, maxZ, grain.count);
 min = vec3(minX, minY, minZ);
 max = vec3(maxX, maxY, maxZ);
}

void Simulation::domainWire(vec3& min, vec3& max) {
 float* const Px = wire.Px.begin(), *Py = wire.Py.begin(), *Pz = wire.Pz.begin();
 vXsf minX_ = _0f, minY_ = _0f, minZ_ = _0f, maxX_ = _0f, maxY_ = _0f, maxZ_ = _0f;
 for(size_t i=0; i<wire.count; i+=simd) {
  vXsf X = load(Px, i), Y = load(Py, i), Z = load(Pz, i);
  minX_ = ::min(minX_, X);
  maxX_ = ::max(maxX_, X);
  minY_ = ::min(minY_, Y);
  maxY_ = ::max(maxY_, Y);
  minZ_ = ::min(minZ_, Z);
  maxZ_ = ::max(maxZ_, Z);
 }
 const float minX = ::min(minX_);
 const float minY = ::min(minY_);
 const float minZ = ::min(minZ_);
 const float maxX = ::max(maxX_);
 const float maxY = ::max(maxY_);
 const float maxZ = ::max(maxZ_);
 assert_(maxX-minX < 16 && maxY-minY < 16 && maxZ-minZ < 16, "wire");
 min = vec3(minX, minY, minZ);
 max = vec3(maxX, maxY, maxZ);
}

void Simulation::step() {
 stepProcess();

 grainTotalTime.start();
 stepGrain();
 stepGrainBottom();
 stepGrainTop();
 stepGrainGrain();
 grainTotalTime.stop();

 membraneTotalTime.start();
 stepMembrane();
 membraneTotalTime.stop();
 stepGrainMembrane();

 /*stepWire();
 stepGrainWire();
 stepWireTension();
 stepWireBendingResistance();
 stepWireBottom();*/

 grainTotalTime.start();
 stepGrainIntegration();
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
 if(stepTimeRT.nanoseconds()*100<totalTime.nanoseconds()*99)
  log("step", strD(stepTimeRT, totalTime));
 if(grainWireContactSizeSum) {
  log("grain-wire contact count mean", grainWireContactSizeSum/timeStep);
  log("grain-wire cycle/grain", (float)grainWireEvaluateTime/grainWireContactSizeSum);
  log("grain-wire B/cycle", (float)(grainWireContactSizeSum*41*4)/grainWireEvaluateTime);
 }
 log("W", membrane.W, "H", membrane.H, "W*H", membrane.W*membrane.H, grain.count, wire.count);
 const bool reset = true;
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
 logTime(domain);
 logTime(memory);
 logTime(grainGrainLattice);
 logTime(grainGrainSearch);
 logTime(grainGrainFilter);
 logTime(grainGrainEvaluate);
 logTime(grainGrainSum);
 logTime(grainWireSearch);
 logTime(grainWireFilter);
 logTime(grainWireEvaluate);
 logTime(grainWireSum);
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

 logTime(wireInitialization);
 logTime(wireTension);
 logTime(wireBendingResistance);
 logTime(wireBottomFilter);
 logTime(wireBottomEvaluate);
 logTime(wireBottomSum);
 logTime(wireIntegration);
#undef logTime
 for(const auto entry: profile) log(strD(entry.key, stepTime), entry.value);
 log(strD(shown, stepTime), "/", strD(accounted, stepTime));
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
 for(int unused t: range(1/(dt*60*32))) step();
 stepTime.stop();
 stepTimeRT.stop();
 if(timeStep%(1*size_t(1/(dt*60))) == 0) {
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
