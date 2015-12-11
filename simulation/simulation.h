#include "system.h"
#include "time.h"
#include "variant.h"

// High level simulation and contact management
struct Simulation : System {
 // Process parameters
 sconst float Gz = -10 * N/kg; // Gravity
 const float patternRadius = membrane.radius - Grain::radius;
 enum Pattern { None, Helix, Cross, Loop };
 sconst string patterns[] {"none", "helix", "radial", "spiral"};
 const Pattern pattern;
 const float linearSpeed = 4 * m/s;
 const float verticalSpeed = 0.2 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));
 const float plateSpeed = 0.01 * m/s;

 // Process variables
 enum ProcessState { Pour, Pressure, Load, Error };
 //sconst string processStates[] {"pour", "load", "error"};
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentWinchRadius = patternRadius;
 float pressure = 0;
 float bottomZ = 0, topZ = membrane.height, topZ0;

 // Results
 float bottomForceZ = 0, topForceZ = 0;

 // Grain-Bottom
 buffer<uint> oldGrainBottomA;
 buffer<float> oldGrainBottomLocalAx;
 buffer<float> oldGrainBottomLocalAy;
 buffer<float> oldGrainBottomLocalAz;
 buffer<float> oldGrainBottomLocalBx;
 buffer<float> oldGrainBottomLocalBy;
 buffer<float> oldGrainBottomLocalBz;

 buffer<uint> grainBottomContact;

 buffer<uint> grainBottomA;
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
 buffer<uint> oldGrainTopA;
 buffer<float> oldGrainTopLocalAx;
 buffer<float> oldGrainTopLocalAy;
 buffer<float> oldGrainTopLocalAz;
 buffer<float> oldGrainTopLocalBx;
 buffer<float> oldGrainTopLocalBy;
 buffer<float> oldGrainTopLocalBz;

 buffer<uint> grainTopContact;

 buffer<uint> grainTopA;
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
 buffer<uint> oldWireBottomA;
 buffer<float> oldWireBottomLocalAx;
 buffer<float> oldWireBottomLocalAy;
 buffer<float> oldWireBottomLocalAz;
 buffer<float> oldWireBottomLocalBx;
 buffer<float> oldWireBottomLocalBy;
 buffer<float> oldWireBottomLocalBz;

  buffer<uint> wireBottomContact;

 buffer<uint> wireBottomA;
 buffer<float> wireBottomLocalAx;
 buffer<float> wireBottomLocalAy;
 buffer<float> wireBottomLocalAz;
 buffer<float> wireBottomLocalBx;
 buffer<float> wireBottomLocalBy;
 buffer<float> wireBottomLocalBz;

 buffer<float> wireBottomFx;
 buffer<float> wireBottomFy;
 buffer<float> wireBottomFz;

 // Grain - Grain
 float maxGrainV = 0;
 float grainGrainGlobalMinD = 0;
 uint grainGrainSkipped = 0;

 buffer<uint> oldGrainGrainA ;
 buffer<uint> oldGrainGrainB;
 buffer<float> oldGrainGrainLocalAx;
 buffer<float> oldGrainGrainLocalAy;
 buffer<float> oldGrainGrainLocalAz;
 buffer<float> oldGrainGrainLocalBx;
 buffer<float> oldGrainGrainLocalBy;
 buffer<float> oldGrainGrainLocalBz;

 buffer<uint> grainGrainA ;
 buffer<uint> grainGrainB;
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 buffer<uint> grainGrainContact;

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

 buffer<uint> oldGrainWireA;
 buffer<uint> oldGrainWireB;
 buffer<float> oldGrainWireLocalAx;
 buffer<float> oldGrainWireLocalAy;
 buffer<float> oldGrainWireLocalAz;
 buffer<float> oldGrainWireLocalBx;
 buffer<float> oldGrainWireLocalBy;
 buffer<float> oldGrainWireLocalBz;

 buffer<uint> grainWireA;
 buffer<uint> grainWireB;
 buffer<float> grainWireLocalAx;
 buffer<float> grainWireLocalAy;
 buffer<float> grainWireLocalAz;
 buffer<float> grainWireLocalBx;
 buffer<float> grainWireLocalBy;
 buffer<float> grainWireLocalBz;

 buffer<uint> grainWireContact;

 buffer<float> grainWireFx;
 buffer<float> grainWireFy;
 buffer<float> grainWireFz;
 buffer<float> grainWireTAx;
 buffer<float> grainWireTAy;
 buffer<float> grainWireTAz;

 // Grain - Membrane
 float grainMembraneGlobalMinD = 0;
 uint grainMembraneSkipped = 0;

 buffer<uint> oldGrainMembraneA;
 buffer<uint> oldGrainMembraneB;
 buffer<float> oldGrainMembraneLocalAx;
 buffer<float> oldGrainMembraneLocalAy;
 buffer<float> oldGrainMembraneLocalAz;
 buffer<float> oldGrainMembraneLocalBx;
 buffer<float> oldGrainMembraneLocalBy;
 buffer<float> oldGrainMembraneLocalBz;

 buffer<uint> grainMembraneA;
 buffer<uint> grainMembraneB;
 buffer<float> grainMembraneLocalAx;
 buffer<float> grainMembraneLocalAy;
 buffer<float> grainMembraneLocalAz;
 buffer<float> grainMembraneLocalBx;
 buffer<float> grainMembraneLocalBy;
 buffer<float> grainMembraneLocalBz;

 buffer<uint> grainMembraneContact;

 buffer<float> grainMembraneFx;
 buffer<float> grainMembraneFy;
 buffer<float> grainMembraneFz;
 buffer<float> grainMembraneTAx;
 buffer<float> grainMembraneTAy;
 buffer<float> grainMembraneTAz;

 Simulation(const Dict& p);

 //void domainMembrane(vec3& min, vec3& max);
 //void domainGrain(vec3& min, vec3& max);
 //void c(vec3& min, vec3& max);

 void step();

 void stepProcess();
  tsc processTime;
 tsc grainTotalTime;
 void stepGrain();
  uint64 grainTime = 0;
 void stepGrainBottom();
  //tsc grainBottomSearchTime; // TODO: verlet
  uint64 grainBottomFilterTime = 0;
  uint64 grainBottomEvaluateTime = 0;
  tsc grainBottomSumTime;
 void stepGrainTop();
  //tsc grainTopSearchTime; // TODO: verlet
  uint64 grainTopFilterTime = 0;
  uint64 grainTopEvaluateTime = 0;
  tsc grainTopSumTime;
 void stepGrainSide();
  //tsc grainSideSearchTime; // TODO: verlet
  tsc grainSideFilterTime;
  tsc grainSideEvaluateTime;
  tsc grainSideSumTime;
 void stepGrainGrain();
  tsc grainGrainTotalTime;
  uint64 domainTime = 0;
  tsc memoryTime;
  tsc grainGrainLatticeTime;
  tsc grainGrainSearchTime;
  uint64 grainGrainFilterTime = 0;
  uint64 grainGrainEvaluateTime = 0;
  tsc grainGrainSumTime;
 void stepGrainIntegration();
  uint64 grainIntegrationTime = 0;

 tsc membraneTotalTime;
 void stepMembrane();
  uint64 membraneInitializationTime = 0;
  uint64 membraneForceTime = 0;
 void stepGrainMembrane();
   tsc grainMembraneGridTime;
   uint64 grainMembraneSearchTime = 0;
   uint64 grainMembraneFilterTime = 0;
   //tsc grainMembraneEvaluateTime;
   uint64 grainMembraneEvaluateTime = 0;
   tsc grainMembraneSumTime;
   size_t grainMembraneContactSizeSum;
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
 //tsc wireBottomSearchTime; // TODO: verlet
  uint64 wireBottomFilterTime = 0;
  uint64 wireBottomEvaluateTime = 0;
  tsc wireBottomSumTime;
  void stepGrainWire();
   tsc grainWireLatticeTime = 0;
   uint64 grainWireSearchTime = 0;
   uint64 grainWireFilterTime = 0;
   //tsc grainWireEvaluateTime;
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

