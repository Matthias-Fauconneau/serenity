#include "system.h"
#include "time.h"
#include "variant.h"
#include "lattice.h"

// High level simulation and contact management
struct Simulation : System {
 // Process parameters
 float Gz = -10 * N/kg; // Gravity
 const float verticalSpeed = 1 * m/s;
 const float plateSpeed = 0.01 * m/s;
#if WIRE
 const float patternRadius = membrane.radius - Grain::radius;
 enum Pattern { None, Helix, Cross, Loop };
 sconst string patterns[] {"none", "helix", "radial", "spiral"};
 const Pattern pattern;
 const float linearSpeed = 4 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));
#endif

 // Process variables
 enum ProcessState { Pour, Pressure, Load, Error };
 //sconst string processStates[] {"pour", "load", "error"};
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 const float targetPressure = 80 * KPa;
 float pressure = targetPressure/128;
 float bottomZ = 0, topZ = membrane.height, topZ0;
#if WIRE
 float lastAngle = 0, winchAngle = 0, currentWinchRadius = patternRadius;
#endif

 // Results
 float bottomForceZ = 0, topForceZ = 0;

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
 const float grainLatticeR = membrane.radius*(1+1./2);
 Lattice<int32> lattice {sqrt(3.)/(2*Grain::radius), vec3(vec2(-grainLatticeR), -Grain::radius),
                                                                                    vec3(vec2(grainLatticeR), membrane.height+Grain::radius)};
 bool validGrainLattice = false;

 // Grain - Grain
 float maxGrainV = 0;
 float grainGrainGlobalMinD = 0;
 uint grainGrainSkipped = 0;

 buffer<int> oldGrainGrainA ;
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
 float grainWireGlobalMinD = 0;
 uint grainWireSkipped = 0;

 buffer<int> oldGrainWireA;
 buffer<int> oldGrainWireB;
 buffer<float> oldGrainWireLocalAx;
 buffer<float> oldGrainWireLocalAy;
 buffer<float> oldGrainWireLocalAz;
 buffer<float> oldGrainWireLocalBx;
 buffer<float> oldGrainWireLocalBy;
 buffer<float> oldGrainWireLocalBz;

 buffer<int> grainWireA;
 buffer<int> grainWireB;
 buffer<float> grainWireLocalAx;
 buffer<float> grainWireLocalAy;
 buffer<float> grainWireLocalAz;
 buffer<float> grainWireLocalBx;
 buffer<float> grainWireLocalBy;
 buffer<float> grainWireLocalBz;

 buffer<int> grainWireContact;

 buffer<float> grainWireFx;
 buffer<float> grainWireFy;
 buffer<float> grainWireFz;
 buffer<float> grainWireTAx;
 buffer<float> grainWireTAy;
 buffer<float> grainWireTAz;

 // Grain - Membrane
 float grainMembraneGlobalMinD = 0;
 uint grainMembraneSkipped = 0;

 buffer<int> oldGrainMembraneA;
 buffer<int> oldGrainMembraneB;
 buffer<float> oldGrainMembraneLocalAx;
 buffer<float> oldGrainMembraneLocalAy;
 buffer<float> oldGrainMembraneLocalAz;
 buffer<float> oldGrainMembraneLocalBx;
 buffer<float> oldGrainMembraneLocalBy;
 buffer<float> oldGrainMembraneLocalBz;

 buffer<int> grainMembraneA;
 buffer<int> grainMembraneB;
 buffer<float> grainMembraneLocalAx;
 buffer<float> grainMembraneLocalAy;
 buffer<float> grainMembraneLocalAz;
 buffer<float> grainMembraneLocalBx;
 buffer<float> grainMembraneLocalBy;
 buffer<float> grainMembraneLocalBz;

 buffer<int> grainMembraneContact;

 buffer<float> grainMembraneFx;
 buffer<float> grainMembraneFy;
 buffer<float> grainMembraneFz;
 buffer<float> grainMembraneTAx;
 buffer<float> grainMembraneTAy;
 buffer<float> grainMembraneTAz;

 Simulation(const Dict& p);

 void grainLattice();

 void step();

 void stepProcess();
  uint64 processTime = 0;
 tsc grainTotalTime;
 void stepGrain();
  uint64 grainTime = 0;
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

#if WIRE
 void stepWire();
  uint64 wireInitializationTime = 0;
 void stepWireTension();
  tsc wireTensionTime;
 void stepWireBendingResistance();
  tsc wireBendingResistanceTime;
 void stepWireBottom();
  uint64 wireBottomSearchTime = 0;
  uint64 wireBottomFilterTime = 0;
  uint64 wireBottomEvaluateTime = 0;
  tsc wireBottomSumTime;
  void stepGrainWire();
   tsc grainWireLatticeTime = 0;
   uint64 grainWireSearchTime = 0;
   uint64 grainWireFilterTime = 0;
   uint64 grainWireEvaluateTime = 0;
   tsc grainWireSumTime;
   size_t grainWireContactSizeSum;
 void stepWireIntegration();
  uint64 wireIntegrationTime = 0;
#endif

  void profile(const Time& totalTime);
  bool run(const Time& totalTime);
   Time stepTimeRT;
   tsc stepTime;
};

