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
#include "wire.h"
//#include "wire-bottom.h"
//#include "wire-grain.h"

constexpr string Simulation::patterns[];
constexpr string Simulation::processStates[];

bool fail = false;
Lock lock;
/*array<vec2x3> lines;
array<int> highlightGrains;
array<int> faces;
array<int> cylinders;*/
array<Force> forces;

Simulation::Simulation(const Dict& p) :
  triaxial(p.value("triaxial", "0"_)!="0"_),
  validation(p.value("validation", "0"_)!="0"_),
  dt((float)p.value("TimeStep", validation ? 1e-6 : 4e-6)*s),
  normalDampingRate((float)p.value("nDamping",validation ? 1./2 : 1.f)*1),
  targetGrainCount(
   p.value("grainRadius", validation ? 2.5f : 20) != 0 ?
     (4*PI*cb((float)p.value("Radius", validation ? 50 : 100)*mm)
   / (4./3*PI*cb(p.value("grainRadius", validation ? 2.5f : 20)*mm))) / (1+0.60/*2*//*611*/)/*-60*/ : 0
   ),
  grain(p.value("grainRadius", validation ? 2.5f : 20)*mm,
           p.value("grainDensity", validation ? 7.8e3 : 1.4e3)*kg/cb(m),
           p.value("grainShearModulus", validation ? /*10000*//*77000*/50000 : 30)*MPa,
           p.value("grainPoissonRatio", validation ? 0.28 : 0.35)*1,
           p.value("grainWallThickness", validation ? 0 : 0.4)*mm,
           targetGrainCount),
  membrane((float)p.value("Radius", validation ? 50 : 100)*mm,
           validation ? grain->radius*(1-1./4) : (grain->radius?:20*mm),
           validation ? 4 : 2.5),
  wire(grain->radius/2?:10*mm),
  targetDynamicGrainObstacleFrictionCoefficient(validation? 0.228 : 1),
  targetDynamicGrainMembraneFrictionCoefficient(validation ? 0.228 : 0.228),
  targetDynamicGrainGrainFrictionCoefficient(validation ? 0.096 : 1),
  targetDynamicWireGrainFrictionCoefficient(3./2),
  targetDynamicWireBottomFrictionCoefficient(1),
  targetStaticFrictionSpeed((float)p.value("sfSpeed", validation ? 0.1 : 0.05/*0.1*/)*m/s),
  targetStaticFrictionLength((float)p.value("sfLength", validation ? 1e-3f : 4e-4f/*1e-3f*/)*m),
  targetStaticFrictionStiffness((float)p.value("sfStiffness", validation ? 4e3f : 4e3f)/m),
  targetStaticFrictionDamping((float)p.value("sfDamping", 1)*N/(m/s)),
  Gz(-(float)p.value("G", validation?4000:10.f)*N/kg),
  verticalSpeed(p.value("verticalSpeed", validation ? 2 : 0.05f)*m/s),
  linearSpeed(p.value("linearSpeed",1.f)*m/s),
  targetPressure((float)p.value("Pressure", 80e3f)*Pa),
  plateSpeed((float)p.value("Speed", 10)*mm/s),
  patternRadius(membrane->radius - Wire::radius),
  pattern(p.contains("Pattern") ? Pattern(ref<string>(patterns).indexOf(p.at("Pattern")))
                                                : (validation?None:Loop)),
  currentHeight(0?Wire::radius:grain->radius),
  membraneRadius(membrane->radius),
  topZ(membrane->height),
  latticeRadius(triaxial ? membrane->radius+grain->radius : 2*membrane->radius),
  lattice {sqrt(3.)/(2*grain->radius), vec3(vec2(-latticeRadius), -grain->radius*2),
                                           vec3(vec2(latticeRadius), membrane->height+grain->radius)}
{
 log(p, Gz, grain->mass);
 assert(str(p).size < 256, str(p).size);
 if(pattern) { // Initial wire node
  size_t i = wire->count++;
  assert_(wire->count < (int)wire->capacity);
  wire->Px[i] = patternRadius;
  wire->Py[i] = 0;
  wire->Pz[i] = currentHeight+grain->radius+Wire::radius;
  wire->Vx[i] = 0; wire->Vy[i] = 0; wire->Vz[i] = 0;
  winchAngle += wire->internodeLength / currentWinchRadius;
 }
 if(!triaxial) {
  dynamicGrainObstacleFrictionCoefficient = targetDynamicGrainObstacleFrictionCoefficient;
  dynamicGrainMembraneFrictionCoefficient = targetDynamicGrainMembraneFrictionCoefficient;
  dynamicGrainGrainFrictionCoefficient = targetDynamicGrainGrainFrictionCoefficient;
  dynamicWireGrainFrictionCoefficient = targetDynamicWireGrainFrictionCoefficient;
  dynamicWireBottomFrictionCoefficient = targetDynamicWireBottomFrictionCoefficient;
  staticFrictionSpeed = targetStaticFrictionSpeed;
  staticFrictionLength = targetStaticFrictionLength;
  staticFrictionStiffness = targetStaticFrictionStiffness;
  staticFrictionDamping = targetStaticFrictionDamping;
 }
}
Simulation::~Simulation() {}

void Simulation::step() {
 if(processState == Release) {
  /*if(membraneViscosity < targetViscosity) membraneViscosity += 10 * dt;
  if(membraneViscosity > targetViscosity) membraneViscosity = targetViscosity;*/
  constexpr float membraneRadiusSpeed = 0.04*m/s;
  if(membraneRadius < latticeRadius) membraneRadius += membraneRadiusSpeed * dt;
  if(membraneRadius > latticeRadius) membraneRadius  = latticeRadius;
  float maxGrainMembraneV = maxGrainV + membraneRadiusSpeed;
  grainMembraneGlobalMinD -= maxGrainMembraneV * this->dt;
  const int W = membrane->W;
  const int stride = membrane->stride;
  const int margin = membrane->margin;
  for(size_t i: range(membrane->H)) {
   for(size_t j: range(membrane->W)) {
    float z = i*membrane->height/(membrane->H-1);
    float a = 2*PI*(j+(i%2)*1./2)/membrane->W;
    float x = membraneRadius*cos(a), y = membraneRadius*sin(a);
    membrane->Px[i*stride+margin+j] = x;
    membrane->Py[i*stride+margin+j] = y;
    membrane->Pz[i*stride+margin+j] = z;
   }
   // Copies position back to repeated nodes
   membrane->Px[i*stride+margin-1] = membrane->Px[i*stride+margin+W-1];
   membrane->Py[i*stride+margin-1] = membrane->Py[i*stride+margin+W-1];
   membrane->Pz[i*stride+margin-1] = membrane->Pz[i*stride+margin+W-1];
   membrane->Px[i*stride+margin+W] = membrane->Px[i*stride+margin+0];
   membrane->Py[i*stride+margin+W] = membrane->Py[i*stride+margin+0];
   membrane->Pz[i*stride+margin+W] = membrane->Pz[i*stride+margin+0];
  }
  membranePositionChanged = true;
  if(membraneViscosity == targetViscosity) processState = Released;
 }
 if(nextProcessState > processState) {
  processState = nextProcessState;
  log("processState = ", processStates[processState]);
  if(processState == Release) {
   //membraneViscosity = targetViscosity;
   //wire->count--;
   //for(size_t i: range(wire->count)) { wire->Vx[i] = 0; wire->Vy[i] = 0; wire->Vz[i] = 0; }
   log("Release", wire->count);
  }
 }
 stepTime.start();

 stepProcess();

 if(grain->count) {
  grainTotalTime.start();
  stepGrain();
  grainSideTime.start();
  if(processState >= Released) { // Reprojects any grain outside lattice bounds (when not already constrained)
   assert_(latticeRadius == 2*membrane->radius);
   if(latticeRadius < 2*membrane->radius) {
    latticeRadius = 2*membrane->radius;
    lattice.~Lattice<int32>();
    new (&lattice) Lattice<int32>(
       sqrt(3.)/(2*grain->radius), vec3(vec2(-latticeRadius), -grain->radius*2),
       vec3(vec2(latticeRadius), membrane->height+grain->radius));
    validGrainLattice = false;
   }
   float R = latticeRadius;
   for(size_t i: range(grain->count)) {
    vec2 p = grain->position(i).xy();
    float l = length(p);
    if(l > R) {
     vec2 np = p*R/l;
     grain->Px[simd+i] = np.x;
     grain->Py[simd+i] = np.y;
     grain->Vx[simd+i] = 0;
     grain->Vy[simd+i] = 0;
    }
   }
  }
  grainSideTime.stop();
  grainBottomTotalTime.start();
  stepGrainBottom();
  grainBottomTotalTime.stop();
  if(/*processState < Released*/triaxial) {
   grainTopTotalTime.start();
   stepGrainTop();
   grainTopTotalTime.stop();
  }
  grainGrainTotalTime.start();
  stepGrainGrain();
  grainGrainTotalTime.stop();
  grainTotalTime.stop();
 }

 if(processState < Released) {
  membraneTotalTime.start();
  if(membraneViscosity) stepMembrane();
  if(1) {
   grainMembraneTotalTime.start();
   stepGrainMembrane();
   grainMembraneTotalTime.stop();
  }
  membraneTotalTime.stop();
  if(fail) return;
 }

 stepWire();
 stepWireTension();
 stepWireGrain();
 //stepWireBendingResistance();
 wireBottomTotalTime.start();
 stepWireBottom();
 wireBottomTotalTime.stop();
 stepWireIntegration();

 if(grain->count) {
  grainTotalTime.start();
  stepGrainIntegration();
  validGrainLattice = false;
  grainTotalTime.stop();
 }
 if(/*useMembrane*/processState < Released) {
  membraneTotalTime.start();
  stepMembraneIntegration();
  membraneTotalTime.stop();
 }

 stepTime.stop();

 timeStep++;
}

void Simulation::profile() {
 log("----",timeStep/size_t(1*1/(dt*(60/s))),"----");
 log(totalTime.microseconds()/timeStep, "us/step", totalTime, timeStep);
 //if(stepTimeRT.nanoseconds()*100<totalTime.nanoseconds()*99) log("step", strD(stepTimeRT, totalTime));
 if(wireGrainContactSizeSum) {
  log("grain-wire contact count mean", wireGrainContactSizeSum/timeStep);
  log("grain-wire cycle/grain", (float)wireGrainEvaluateTime/wireGrainContactSizeSum);
  log("grain-wire B/cycle", (float)(wireGrainContactSizeSum*41*4)/wireGrainEvaluateTime);
 }
 log("W", membrane->W, "H", membrane->H, "W*H", membrane->W*membrane->H, grain->count);
 const bool reset = false;
 size_t accounted = 0, shown = 0;
 map<uint64, String> profile; profile.reserve(64);
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

 if(grain->count) {
  profile.insertSortedMulti(grainTotalTime, "=sum{grain}"+
                            strD(grainTime+grainBottomTotalTime+grainTopTotalTime+grainGrainTotalTime
                                 +grainIntegrationTime, grainTotalTime)
                            +" "+strD(grainTime, grainTotalTime)
                            +" "+strD(grainBottomTotalTime, grainTotalTime)
                            +" "+strD(grainTopTotalTime, grainTotalTime)
                            +" "+strD(grainGrainTotalTime, grainTotalTime)
                            +" "+strD(grainIntegrationTime, grainTotalTime));
  if(reset) grainTotalTime.reset();
  logTime(grain);
  logTime(grainSide);
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
  logTime(grainLattice);
 }

 if(/*useMembrane*/processState < Release) {
  profile.insertSortedMulti(grainMembraneTotalTime, "=sum{grain-membrane}"_+
                            strD(grainLatticeTime+grainMembraneSearchTime+grainMembraneFilterTime
                                 +grainMembraneRepackFrictionTime+grainMembraneEvaluateTime
                                 +grainMembraneSumTime, grainMembraneTotalTime));
  if(reset) grainMembraneTotalTime.reset();
  logTime(membraneInitialization);
  logTime(membraneForce);
  profile.insertSortedMulti(membraneTotalTime, "=sum{membrane}"_+
                            strD(membraneInitializationTime+membraneForceTime
                                 +grainMembraneTotalTime+membraneIntegrationTime, membraneTotalTime));
  if(reset) membraneTotalTime.reset();
  logTime(grainMembraneSearch);
  logTime(grainMembraneFilter);
  logTime(grainMembraneRepackFriction);
  logTime(grainMembraneEvaluate);
  logTime(grainMembraneSum);
  logTime(membraneIntegration);
 }

 logTime(wireInitialization);
 logTime(wireTension);
 logTime(wireBendingResistance);

 profile.insertSortedMulti(wireBottomTotalTime, "=sum{wire-bottom}"_+
                           strD(wireBottomSearchTime/*+wireBottomRepackFrictionTime*/+wireBottomFilterTime
                                +wireBottomEvaluateTime+wireBottomSumTime, wireBottomTotalTime));
 if(reset) wireBottomTotalTime.reset();
 logTime(wireBottomSearch);
 logTime(wireBottomFilter);
 logTime(wireBottomEvaluate);
 logTime(wireBottomSum);

 logTime(wireGrainSearch);
 logTime(wireGrainFilter);
 logTime(wireGrainEvaluate);
 logTime(wireGrainSum);

 logTime(wireIntegration);

#undef logTime
 for(const auto entry: profile) {
  assert(entry.key <= stepTime, entry.value, entry.key/1000000.f, stepTime.cycleCount()/1000000.f);
  log(strD(entry.key, stepTime), entry.value);
 }
 log(strD(shown, stepTime), "/", strD(accounted, stepTime)/*, "/", strD(stepTime, totalTimeC)*/);
 if(reset) stepTime.reset();
}

void Simulation::run() {
 totalTime.start();
 totalTimeC.start();
 for(;;) {
  if(stop) break;
  for(int unused t: range(256)) {
   step();
   if(!dump) {
    String name = replace(arguments()[0],"/",":");
    dump = File(name+".dump", currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate));
   }
   buffer<byte> header (64, 0);
   assert_(grain->count < 1<<16);
   header.append(raw(grain->count));
   header.append(raw(grain->radius));
   assert_(uint(membrane->count) < 1<<16);
   header.append(raw(uint(membrane->count)));
   header.append(raw(membrane->W));
   header.append(raw(membrane->H));
   header.append(raw(membrane->stride));
   header.append(raw(membrane->margin));
   header.append(raw(membrane->radius));
   assert_(forces.size < 1<<16);
   header.append(raw(uint(forces.size)));
   assert_(header.size == 9*sizeof(int));
   header.size = 64;
   dump.write(header);
   {
    auto write = [this](const buffer<float>& A) {
     assert_(simd+align(64, grain->count) <= A.capacity, A.size, A.capacity, align(64, A.size));
     dump.write(cast<byte>(ref<float>(A.data+simd, align(64, grain->count))));
    };
    write(grain->Px); write(grain->Py); write(grain->Pz);
    write(grain->Vx); write(grain->Vy); write(grain->Vz);
    write(grain->Fx); write(grain->Fy); write(grain->Fz);
    write(grain->Rx); write(grain->Ry); write(grain->Rz); write(grain->Rw);
    write(grain->AVx); write(grain->AVy); write(grain->AVz);
    write(grain->Tx); write(grain->Ty); write(grain->Tz);
   }
   { // TODO: compact? (no margin, stride=W)
       auto write = [this](const buffer<float>& A) {
        assert_(A.size == membrane->count);
        assert_(align(64, A.size) <= A.capacity, A.size, A.capacity, align(64, A.size));
        dump.write(cast<byte>(ref<float>(A.data, align(64, A.size))));
       };
       write(membrane->Px); write(membrane->Py); write(membrane->Pz);
       write(membrane->Vx); write(membrane->Vy); write(membrane->Vz);
       write(membrane->Fx); write(membrane->Fy); write(membrane->Fz);
   }
   assert_(sizeof(Force) == 6*sizeof(float));
   auto forces = cast<byte>(::forces);
   dump.write(ref<byte>(forces.data, align(64, forces.size)));
   ::forces.clear();
   if(fail) return;
  }
  if(timeStep%65536 == 0) {
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
   float side = 2*PI*membrane->radius*height;
   float radial = (radialSumStepCount?radialForce/radialSumStepCount:0) / side;
   radialForce = 0; radialSumStepCount = 0;
   if(!primed) {
    primed = true;
    String name = replace(arguments()[0],"/",":");
    pressureStrain = File(name, currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate));
    {
     String line = "version:"+str(VERSION)+",voidRatio:"+str(voidRatio)+"\n";
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
