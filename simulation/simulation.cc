// TODO: Factorize force evaluation, contact management
#include "simulation.h"
#include "parallel.h"
#include "grain.h"
#include "membrane.h"
#include "plate.h"
//#include "process.h"
//#include "grain-bottom.h"
//#include "grain-top.h"
//#include "grain-grain.h"
//#include "grain-membrane.h"
//include "wire.h"
//include "wire-bottom.h"
//include "grain-wire.h"

#if GRAIN
constexpr float Grain::radius;
constexpr float Grain::mass;
constexpr float Grain::angularMass;
#endif
#if WIRE
constexpr float System::Wire::radius;
constexpr float System::Wire::mass;
constexpr float System::Wire::internodeLength;
constexpr float System::Wire::tensionStiffness;
constexpr float System::Wire::bendStiffness;
constexpr string Simulation::patterns[];
#endif
bool fail = false;
Lock lock;
array<vec2x3> lines;
array<int> faces;

Simulation::Simulation(const Dict& p) :
  dt((float)p.at("TimeStep")*s),
  normalDampingRate((float)p.at("nDamping")*1),
  targetDynamicGrainObstacleFrictionCoefficient(0.228),
  targetDynamicGrainMembraneFrictionCoefficient(0.228),
  targetDynamicGrainGrainFrictionCoefficient(0.096),
  targetDynamicGrainWireFrictionCoefficient(0.228),
  targetStaticFrictionSpeed((float)p.at("sfSpeed")*mm/s),
  targetStaticFrictionLength((float)p.at("sfLength")*m),
  targetStaticFrictionStiffness((float)p.at("sfStiffness")*N/m),
  targetStaticFrictionDamping((float)p.at("sfDamping")*N/(m/s)),
  targetGrainCount(4*PI*cb((float)p.at("Radius")*mm)/Grain::volume/(1+0.615)-49),
  grain(targetGrainCount),
  membrane((float)p.at("Radius")*mm),
  //#if DEBUG
  //Gz( -10 * N/kg),
  //#else
  Gz(1 * -10 * N/kg),
  //Gz(5000 * -10 * N/kg),
  //#endif
  targetPressure((float)p.at("Pressure")*Pa),
  plateSpeed((float)p.at("Speed")*mm/s),
  currentHeight(Grain::radius),
  topZ(membrane->height),
  lattice {sqrt(3.)/(2*Grain::radius), vec3(vec2(-(membrane->radius*3./2/*+Grain::radius*/)), -Grain::radius*2),
                                           vec3(vec2(membrane->radius*3./2/*+Grain::radius*/), membrane->height+Grain::radius)}
#if WIRE
, pattern(p.contains("Pattern")?Pattern(ref<string>(patterns).indexOf(p.at("Pattern"))):None)
#endif
{
 log(targetGrainCount);
#if WIRE
 if(pattern) { // Initial wire node
  size_t i = wire.count++;
  wire.Px[i] = patternRadius; wire.Py[i] = 0; wire.Pz[i] = currentHeight+Grain::radius+Wire::radius;
  wire.Vx[i] = 0; wire.Vy[i] = 0; wire.Vz[i] = 0;
  winchAngle += Wire::internodeLength / currentWinchRadius;
 }
#endif
}
Simulation::~Simulation() {}

void Simulation::step() {
 stepTime.start();

 stepProcess();

 grainTotalTime.start();
 stepGrain();
 grainBottomTotalTime.start();
 stepGrainBottom();
 grainBottomTotalTime.stop();
 grainTopTotalTime.start();
 stepGrainTop();
 grainTopTotalTime.stop();
 grainGrainTotalTime.start();
 stepGrainGrain();
 grainGrainTotalTime.stop();
 grainTotalTime.stop();

 membraneTotalTime.start();
 if(membraneViscosity) stepMembrane();
 grainMembraneTotalTime.start();
 stepGrainMembrane();
 grainMembraneTotalTime.stop();
 membraneTotalTime.stop();
 if(fail) return;

#if WIRE
 stepWire();
 stepGrainWire();
 stepWireTension();
 stepWireBendingResistance();
 stepWireBottom();
 stepWireIntegration();
#endif

 grainTotalTime.start();
 stepGrainIntegration();
 validGrainLattice = false;
 grainTotalTime.stop();
 membraneTotalTime.start();
 stepMembraneIntegration();
 membraneTotalTime.stop();

 stepTime.stop();

 timeStep++;
}

void Simulation::profile() {
 log("----",timeStep/size_t(1*1/(dt*(60/s))),"----");
 log(totalTime.microseconds()/timeStep, "us/step", totalTime, timeStep);
 //if(stepTimeRT.nanoseconds()*100<totalTime.nanoseconds()*99) log("step", strD(stepTimeRT, totalTime));
#if WIRE
 if(grainWireContactSizeSum) {
  log("grain-wire contact count mean", grainWireContactSizeSum/timeStep);
  log("grain-wire cycle/grain", (float)grainWireEvaluateTime/grainWireContactSizeSum);
  log("grain-wire B/cycle", (float)(grainWireContactSizeSum*41*4)/grainWireEvaluateTime);
 }
#endif
 log("W", membrane->W, "H", membrane->H, "W*H", membrane->W*membrane->H, grain->count);
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
 log(strD(shown, stepTime), "/", strD(accounted, stepTime));
 if(reset) stepTime.reset();
}

void Simulation::run() {
 totalTime.start();
 for(;;) {
  for(int unused t: range(256)) { step(); if(fail) return; }
  if(timeStep%4096 == 0) {
   profile();
   if(processState == Pour) log(grain->count, "/", targetGrainCount);
   if(processState == Pressure) log(
      "Pressure", int(round(pressure)), "Pa",
      "Membrane viscosity", membraneViscosity );
  }
  if(processState == Load) {
   float height = topZ-bottomZ;
   float strain = 1-height/topZ0;
   float area = PI*sq(membrane->radius) * 2 /*2 plates*/;
   float force = topForceZ/topSumStepCount + bottomForceZ/bottomSumStepCount;
   topForceZ = 0; topSumStepCount = 0;
   bottomForceZ = 0; bottomSumStepCount = 0;
   float stress = force / area;
#if RADIAL
   float side = 2*PI*membrane->radius*height;
   float radial = (radialSumStepCount?radialForce/radialSumStepCount:0) / side;
   radialForce = 0; radialSumStepCount = 0;
#endif
   if(!primed) {
    primed = true;
    //if(existsFile(arguments()[0])) log("Overwrote", arguments()[0]);
    String name = replace(/*replace(*/arguments()[0]/*,"=",":")*/,"/",":");
    if(!existsFile(name))
     pressureStrain = File(name, currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate));
    {
     String line = "voidRatio:"+str(voidRatio)+"\n";
     log_(line);
     if(pressureStrain) pressureStrain.write(line);
    }
    {
     String line = "Strain (%), ""Radial (Pa), ""Axial (Pa)""\n"__;
     log_(line);
     if(pressureStrain) pressureStrain.write(line);
    }
   }
   {String line = str(strain*100, 4u)+' '+str(int(round(radial/Pa)))+' '+str(int(round(stress/Pa)))+'\n';
    log_(line);
    if(pressureStrain) pressureStrain.write(line);
   }
   if(strain > 1./8) break;
  }
 }
 totalTime.stop();
}
