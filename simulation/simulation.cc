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
//#include "wire.h"
//#include "wire-bottom.h"
//#include "grain-wire.h"

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

void Simulation::domain(vec3& min, vec3& max) {
 //min = __builtin_inff(), max = -__builtin_inff();
 float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
 domainTime.start();
 {
  float minX_[maxThreadCount]; mref<float>(minX_, maxThreadCount).clear(0);
  float minY_[maxThreadCount]; mref<float>(minY_, maxThreadCount).clear(0);
  float minZ_[maxThreadCount]; mref<float>(minZ_, maxThreadCount).clear(0);
  float maxX_[maxThreadCount]; mref<float>(maxX_, maxThreadCount).clear(0);
  float maxY_[maxThreadCount]; mref<float>(maxY_, maxThreadCount).clear(0);
  float maxZ_[maxThreadCount]; mref<float>(maxZ_, maxThreadCount).clear(0);
//#pragma omp parallel for
//  for(int i=1; i<membrane.H-1; i++) {
  parallel_for(1, membrane.H-1, [this, &minX_, &minY_, &minZ_, &maxX_, &maxY_, &maxZ_](uint id, uint i) {
   float* const Px = membrane.Px.begin(), *Py = membrane.Py.begin(), *Pz = membrane.Pz.begin();
   vXsf minX = _0f, minY = _0f, minZ = _0f, maxX = _0f, maxY = _0f, maxZ = _0f;
   int stride = membrane.stride, margin=membrane.margin;
   int base = margin+i*stride;
   for(int j=0; j<membrane.W; j+=simd) {
    vXsf X = load(Px, base+j), Y = load(Py, base+j), Z = load(Pz, base+j);
    minX = ::min(minX, X);
    maxX = ::max(maxX, X);
    minY = ::min(minY, Y);
    maxY = ::max(maxY, Y);
    minZ = ::min(minZ, Z);
    maxZ = ::max(maxZ, Z);
   }
   minX_[id] = ::min(minX);
   minY_[id] = ::min(minY);
   minZ_[id] = ::min(minZ);
   maxX_[id] = ::max(maxX);
   maxY_[id] = ::max(maxY);
   maxZ_[id] = ::max(maxZ);
  });
 //}
  for(size_t k: range(threadCount())) {
   minX = ::min(minX, minX_[k]);
   maxX = ::max(maxX, maxX_[k]);
   minY = ::min(minY, minY_[k]);
   maxY = ::max(maxY, maxY_[k]);
   minZ = ::min(minZ, minZ_[k]);
   maxZ = ::max(maxZ, maxZ_[k]);
  }
 }
 this->maxGrainV = maxGrainV;
 //assert_(::max(::max((max-min).x, (max-min).y), (max-min).z) < 16, "membrane");
 {
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
  minX = ::min(minX, ::min(minX_));
  minY = ::min(minY, ::min(minY_));
  minZ = ::min(minZ, ::min(minZ_));
  maxX = ::max(maxX, ::max(maxX_));
  maxY = ::max(maxY, ::max(maxY_));
  maxZ = ::max(maxZ, ::max(maxZ_));
  //assert_(::max(::max((max-min).x, (max-min).y), (max-min).z) < 16, "grain");
 }
 {
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
  minX = ::min(minX, ::min(minX_));
  minY = ::min(minY, ::min(minY_));
  minZ = ::min(minZ, ::min(minZ_));
  maxX = ::max(maxX, ::max(maxX_));
  maxY = ::max(maxY, ::max(maxY_));
  maxZ = ::max(maxZ, ::max(maxZ_));
 }
 min = vec3(minX, minY, minZ);
 max = vec3(maxX, maxY, maxZ);
 assert_(::max(::max((max-min).x, (max-min).y), (max-min).z) < 16, "wire");
 domainTime.stop();
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
 //stepGrainMembrane();

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
 size_t accounted = 0, shown = 0;
 map<uint64, string> profile;
#define logTime(name) \
 accounted += name##Time; \
 if(name##Time > stepTime/64) { \
  profile.insertSortedMulti(name ## Time, #name); \
  shown += name##Time; \
 }
 logTime(process);

 profile.insertSortedMulti(grainTotalTime, "=sum{grain}");
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
 for(const auto entry: profile) log(strD(entry.key, stepTime), entry.value);
 log(strD(shown, stepTime), "/", strD(accounted, stepTime));
#undef log
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
 for(int unused t: range(1/(dt*60))) step();
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
