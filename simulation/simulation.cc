// TODO: Factorize force evaluation, contact management
#include "simulation.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-side.h"
//#include "grain-grain.h"
//#include "wire.h"
//#include "grain-wire.h"
//#include "wire-bottom.h"
#include "parallel.h"

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

void Simulation::step() {
 stepProcess();

 stepGrain();
 stepGrainBottom();
 if(processState < ProcessState::Done) stepGrainSide();
 stepGrainGrain();

 stepWire();
 stepGrainWire();
 stepWireTension();
 stepWireBendingResistance();
 stepWireBottom();
 stepGrainIntegration();
 stepWireIntegration();

 timeStep++;
}

void Simulation::stepGrain() {
 grainTime.start();
 for(size_t a: range(grain.count)) {
  grain.Fx[a] = 0; grain.Fy[a] = 0; grain.Fz[a] = Grain::mass * Gz;
  grain.Tx[a] = 0; grain.Ty[a] = 0; grain.Tz[a] = 0;
 }
 grainTime.stop();
}

void Simulation::stepGrainIntegration() {
 const v8sf dt_mass = float8(grain.dt_mass), dt = float8(this->dt);
 const float dt_angularMass = grain.angularMass;
 v8sf maxGrainV8 = _0f;
 const float* Fx = grain.Fx.data, *Fy = grain.Fy.data, *Fz = grain.Fz.data;
 const float* Tx = grain.Tx.begin(), *Ty = grain.Ty.begin(), *Tz = grain.Tz.begin();
 float* const pVx = grain.Vx.begin(), *pVy = grain.Vy.begin(), *pVz = grain.Vz.begin();
 float* const Px = grain.Px.begin(), *Py = grain.Py.begin(), *Pz = grain.Pz.begin();
 float* const AVx = grain.AVx.begin(), *AVy = grain.AVy.begin(), *AVz = grain.AVz.begin();
 float* const Rx = grain.Rx.begin(), *Ry = grain.Ry.begin(), *Rz = grain.Rz.begin(),
                  *Rw = grain.Rw.begin();
 grainIntegrationTime += parallel_chunk(grain.count/simd, [&](uint, size_t start, size_t size) {
  for(size_t i=start*simd; i<(start+size)*simd; i+=simd) {
    // Symplectic Euler
    v8sf Vx = load(pVx, i), Vy = load(pVy, i), Vz = load(pVz, i);
    Vx += dt_mass * load(Fx, i);
    Vy += dt_mass * load(Fy, i);
    Vz += dt_mass * load(Fz, i);
    store(pVx, i, Vx);
    store(pVy, i, Vy);
    store(pVz, i, Vz);
    store(Px, i, load(Px, i) + dt * Vx);
    store(Py, i, load(Py, i) + dt * Vy);
    store(Pz, i, load(Pz, i) + dt * Vz);
    maxGrainV8 = max(maxGrainV8, sqrt(Vx*Vx + Vy*Vy + Vz*Vz));

    for(size_t k: range(simd)) { // FIXME: SIMD
     size_t j = i+k;
     vec4 dr = this->dt/2 * qmul(vec4(AVx[j],AVy[j],AVz[j], 0), vec4(Rx[j],Ry[j],Rz[j],Rw[j]));
     Rx[j] += dr.x;
     Ry[j] += dr.y;
     Rz[j] += dr.z;
     Rw[j] += dr.w;
     AVx[j] += dt_angularMass * Tx[j];
     AVy[j] += dt_angularMass * Ty[j];
     AVz[j] += dt_angularMass * Tz[j];
     float scale = 1./length(vec4(Rx[j],Ry[j],Rz[j],Rw[j]));
     Rx[j] *= scale;
     Ry[j] *= scale;
     Rz[j] *= scale;
     Rw[j] *= scale;
    }
  }
 }, 1);
 float maxGrainV = 0;
 for(size_t k: range(simd)) maxGrainV = ::max(maxGrainV, maxGrainV8[k]);
 this->maxGrainV = maxGrainV;
 float maxGrainGrainV = maxGrainV + maxGrainV;
 grainGrainGlobalMinD -= maxGrainGrainV * this->dt;
}

#if DEBUG
void Simulation::invariant_() {
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
 map<uint64, string> profile;
#define logTime(name) \
 accounted += name##Time; \
 if(name##Time > stepTime/64) { \
  profile.insertSortedMulti(name ## Time, #name); \
  shown += name##Time; \
 }
 logTime(process);
 logTime(grain);
 logTime(grainBottomFilter);
 logTime(grainBottomEvaluate);
 logTime(grainBottomSum);
 logTime(grainSideFilter);
 logTime(grainSideEvaluate);
 logTime(grainSideSum);
 logTime(grainGrainSearch);
 logTime(grainGrainFilter);
 logTime(grainGrainEvaluate);
 logTime(grainGrainSum);
 logTime(grainWireSearch);
 logTime(grainWireFilter);
 logTime(grainWireEvaluate);
 logTime(grainWireSum);
 logTime(grainIntegration);
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
 for(int unused t: range(1/(dt*60))) step();
 stepTime.stop();
 stepTimeRT.stop();
 if(timeStep%(2*60*size_t(1/(dt*60))) == 0) {
  log(coreFrequencies());
  profile(totalTime);
 }
 return true;
}
