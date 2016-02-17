#include "system.h"
#include "time.h"
#include "variant.h"
#include "lattice.h"
#include "file.h"

struct Grain;
struct Membrane;
struct Plate;
struct Wire;

extern bool fail;
extern Lock lock;
/*struct vec2x3 { vec3 a, b; };
extern array<vec2x3> lines;
extern array<int> faces;
extern array<int> cylinders;*/
extern array<int> highlightGrains;
//struct Force { vec3 origin, force; };
//extern array<Force> forces;

// High level simulation and contact management
struct Simulation {
 //const bool triaxial, validation;
 static constexpr bool triaxial = true, validation = true;

 const float dt;
 size_t timeStep = 0;
 bool stop = false;
 const float normalDampingRate;

 const int targetGrainCount;
 unique<Grain> grain;
 unique<Membrane> membrane;
 unique<Plate> plate;
 unique<Wire> wire;

 // Friction parameters
 const float targetDynamicGrainObstacleFrictionCoefficient;
 float dynamicGrainObstacleFrictionCoefficient = 0;
 const float targetDynamicGrainMembraneFrictionCoefficient;
 float dynamicGrainMembraneFrictionCoefficient = 0;
 const float targetDynamicGrainGrainFrictionCoefficient;
 float dynamicGrainGrainFrictionCoefficient = 0;
 const float targetDynamicWireGrainFrictionCoefficient;
 float dynamicWireGrainFrictionCoefficient = 0;
 const float targetDynamicWireBottomFrictionCoefficient;
 float dynamicWireBottomFrictionCoefficient = 0;
 const float targetStaticFrictionSpeed;
 float staticFrictionSpeed;
 const float targetStaticFrictionLength;
 float staticFrictionLength;
 const float targetStaticFrictionStiffness;
 float staticFrictionStiffness;
 const float targetStaticFrictionDamping;
 float staticFrictionDamping;

 // Process parameters
 float Gz;
 const float verticalSpeed;
 const float linearSpeed;
 const float targetPressure;
 const float plateSpeed;

 const float patternRadius;
 enum Pattern { None, Helix, Cross, Loop };
 sconst string patterns[] {"none"_, "helix"_, "radial"_, "spiral"_};
 const Pattern pattern;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 enum ProcessState { Pour, Pressure, Load, Release, Released, Error };
 sconst string processStates[] {"pour"_, "pressure"_, "load"_, "release"_, "released"_, "error"_};
 ProcessState processState = Pour;
 ProcessState nextProcessState = Pour;
 Random random;
 float currentHeight;
 float pressure = targetPressure;
 size_t lastGrainSpawnTimeStep = 0;
 float grainViscosity = 1-2*dt;
 float angularViscosity = 1-1*dt;
 const float targetViscosity = 1-2*dt;
 float wireViscosity = 1-1*dt;
 float membraneViscosity = 0;
 float membraneRadius;
 bool membranePositionChanged = false;
 float bottomZ = 0, topZ, topZ0;
 float latticeRadius;
 float lastAngle = 0, winchAngle = 0, currentWinchRadius = patternRadius;

 // Results
 int bottomSumStepCount = 0; float bottomForceZ = 0;
 int topSumStepCount = 0; float topForceZ = 0;
 int radialSumStepCount = 0; float radialForce = 0;

 // Grain-Bottom
 buffer<int> oldGrainBottomA;
 buffer<float> oldGrainBottomLocalAx;
 buffer<float> oldGrainBottomLocalAy;
 buffer<float> oldGrainBottomLocalAz;
 buffer<float> oldGrainBottomLocalBx;
 buffer<float> oldGrainBottomLocalBy;
 buffer<float> oldGrainBottomLocalBz;

 buffer<int> grainBottomContact;

 buffer<int> grainBottomA;
 buffer<float> grainBottomLocalAx;
 buffer<float> grainBottomLocalAy;
 buffer<float> grainBottomLocalAz;
 buffer<float> grainBottomLocalBx;
 buffer<float> grainBottomLocalBy;
 buffer<float> grainBottomLocalBz;

 buffer<float> grainBottomFx;
 buffer<float> grainBottomFy;
 buffer<float> grainBottomFz;
 buffer<float> grainBottomTAx;
 buffer<float> grainBottomTAy;
 buffer<float> grainBottomTAz;

 // Grain-Top
 buffer<int> oldGrainTopA;
 buffer<float> oldGrainTopLocalAx;
 buffer<float> oldGrainTopLocalAy;
 buffer<float> oldGrainTopLocalAz;
 buffer<float> oldGrainTopLocalBx;
 buffer<float> oldGrainTopLocalBy;
 buffer<float> oldGrainTopLocalBz;

 buffer<int> grainTopContact;

 buffer<int> grainTopA;
 buffer<float> grainTopLocalAx;
 buffer<float> grainTopLocalAy;
 buffer<float> grainTopLocalAz;
 buffer<float> grainTopLocalBx;
 buffer<float> grainTopLocalBy;
 buffer<float> grainTopLocalBz;

 buffer<float> grainTopFx;
 buffer<float> grainTopFy;
 buffer<float> grainTopFz;
 buffer<float> grainTopTAx;
 buffer<float> grainTopTAy;
 buffer<float> grainTopTAz;

 // Wire-Bottom
 buffer<int> oldWireBottomA;
 buffer<float> oldWireBottomLocalAx;
 buffer<float> oldWireBottomLocalAy;
 buffer<float> oldWireBottomLocalAz;
 buffer<float> oldWireBottomLocalBx;
 buffer<float> oldWireBottomLocalBy;
 buffer<float> oldWireBottomLocalBz;

 buffer<int> wireBottomContact;

 buffer<int> wireBottomA;
 buffer<float> wireBottomLocalAx;
 buffer<float> wireBottomLocalAy;
 buffer<float> wireBottomLocalAz;
 buffer<float> wireBottomLocalBx;
 buffer<float> wireBottomLocalBy;
 buffer<float> wireBottomLocalBz;

 buffer<float> wireBottomFx;
 buffer<float> wireBottomFy;
 buffer<float> wireBottomFz;

 // Grain Lattice
 Lattice<int32> lattice;
 bool validGrainLattice = false;

 // Grain - Grain
 float maxGrainV = 0, maxWireV = 0;
 float grainGrainGlobalMinD = 0;
 uint grainGrainSkipped = 0;

 buffer<int> oldGrainGrainA;
 buffer<int> oldGrainGrainB;
 buffer<float> oldGrainGrainLocalAx;
 buffer<float> oldGrainGrainLocalAy;
 buffer<float> oldGrainGrainLocalAz;
 buffer<float> oldGrainGrainLocalBx;
 buffer<float> oldGrainGrainLocalBy;
 buffer<float> oldGrainGrainLocalBz;

 buffer<int> grainGrainA ;
 buffer<int> grainGrainB;
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 buffer<int> grainGrainContact;

 buffer<float> grainGrainFx;
 buffer<float> grainGrainFy;
 buffer<float> grainGrainFz;
 buffer<float> grainGrainTAx;
 buffer<float> grainGrainTAy;
 buffer<float> grainGrainTAz;
 buffer<float> grainGrainTBx;
 buffer<float> grainGrainTBy;
 buffer<float> grainGrainTBz;

 // Grain - Wire
 float wireGrainGlobalMinD = 0;
 uint wireGrainSkipped = 0;

 buffer<int> oldWireGrainA;
 buffer<int> oldWireGrainB;
 buffer<float> oldWireGrainLocalAx;
 buffer<float> oldWireGrainLocalAy;
 buffer<float> oldWireGrainLocalAz;
 buffer<float> oldWireGrainLocalBx;
 buffer<float> oldWireGrainLocalBy;
 buffer<float> oldWireGrainLocalBz;

 buffer<int> wireGrainA;
 buffer<int> wireGrainB;
 buffer<float> wireGrainLocalAx;
 buffer<float> wireGrainLocalAy;
 buffer<float> wireGrainLocalAz;
 buffer<float> wireGrainLocalBx;
 buffer<float> wireGrainLocalBy;
 buffer<float> wireGrainLocalBz;

 buffer<int> wireGrainContact;

 buffer<float> wireGrainFx;
 buffer<float> wireGrainFy;
 buffer<float> wireGrainFz;
 buffer<float> wireGrainTBx;
 buffer<float> wireGrainTBy;
 buffer<float> wireGrainTBz;

 // Grain - Membrane
 float grainMembraneGlobalMinD = 0;
 uint grainMembraneSkipped = 0;

 struct {
  buffer<int> oldA;
  buffer<int> oldB;
  buffer<float> oldLocalAx;
  buffer<float> oldLocalAy;
  buffer<float> oldLocalAz;
#if MEMBRANE_FACE
  buffer<float> oldLocalBu;
  buffer<float> oldLocalBv;
  //buffer<float> oldLocalBt;
#endif

  buffer<int> A;
  buffer<int> B;
  buffer<float> localAx;
  buffer<float> localAy;
  buffer<float> localAz;
#if MEMBRANE_FACE
  buffer<float> localBu;
  buffer<float> localBv;
  //buffer<float> localBt;
#endif

  buffer<int> contacts;

  buffer<float> Fx;
  buffer<float> Fy;
  buffer<float> Fz;
  buffer<float> TAx;
  buffer<float> TAy;
  buffer<float> TAz;
  buffer<float> U;
  buffer<float> V;
 } grainMembrane[2];

 File dump;
 bool primed = false;
 File pressureStrain;
 float voidRatio = 0;

 Simulation(const Dict& p);
 virtual ~Simulation();

 void grainLattice();

 Time totalTime;
 tsc totalTimeC;
 void step();
  tsc stepTime;
 void stepProcess();
  uint64 processTime = 0;
 tsc grainTotalTime;
 void stepGrain();
  uint64 grainTime = 0;
  tsc grainSideTime;
 void stepGrainBottom();
  tsc grainBottomTotalTime;
  uint64 grainBottomSearchTime = 0;
  uint64 grainBottomFilterTime = 0;
  tsc grainBottomRepackFrictionTime;
  uint64 grainBottomEvaluateTime = 0;
  tsc grainBottomSumTime;
 void stepGrainTop();
  tsc grainTopTotalTime;
  uint64 grainTopSearchTime = 0;
  uint64 grainTopFilterTime = 0;
  tsc grainTopRepackFrictionTime;
  uint64 grainTopEvaluateTime = 0;
  tsc grainTopSumTime;
 void stepGrainGrain();
  tsc grainGrainTotalTime;
  uint64 domainTime = 0;
  tsc memoryTime;
  uint64 grainLatticeTime = 0;
  uint64 grainGrainSearchTime = 0;
  uint64 grainGrainFilterTime = 0;
  tsc grainGrainRepackFrictionTime;
  uint64 grainGrainEvaluateTime = 0;
  tsc grainGrainSumTime;
  uint64 grainGrainSumDomainTime = 0;
  tsc grainGrainSumAllocateTime;
  uint64 grainGrainSumSumTime = 0;
  uint64 grainGrainSumMergeTime = 0;
 void stepGrainIntegration();
  uint64 grainIntegrationTime = 0;

 tsc membraneTotalTime;
 void stepMembrane();
  uint64 membraneInitializationTime = 0;
  uint64 membraneForceTime = 0;
 void stepGrainMembrane();
   tsc grainMembraneTotalTime;
   uint64 grainMembraneSearchTime = 0;
   uint64 grainMembraneFilterTime = 0;
   tsc grainMembraneRepackFrictionTime;
   uint64 grainMembraneEvaluateTime = 0;
   tsc grainMembraneSumTime;
   size_t grainMembraneContactSizeSum = 0;
 void stepMembraneIntegration();
  uint64 membraneIntegrationTime = 0;

 void stepWire();
  uint64 wireInitializationTime = 0;
 void stepWireTension();
  tsc wireTensionTime;
 void stepWireBendingResistance();
  tsc wireBendingResistanceTime;
 void stepWireBottom();
  tsc wireBottomTotalTime;
  uint64 wireBottomSearchTime = 0;
  uint64 wireBottomFilterTime = 0;
  uint64 wireBottomEvaluateTime = 0;
  tsc wireBottomSumTime;
  void stepWireGrain();
   tsc wireGrainLatticeTime;
   uint64 wireGrainSearchTime = 0;
   uint64 wireGrainRepackTime = 0;
   uint64 wireGrainFilterTime = 0;
   uint64 wireGrainEvaluateTime = 0;
   tsc wireGrainSumTime;
   size_t wireGrainContactSizeSum;
 void stepWireIntegration();
  uint64 wireIntegrationTime = 0;

  void profile();
  void run();
};

