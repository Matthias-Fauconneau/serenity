#include "system.h"
#include "lattice.h"
#include "grid.h"
#include "time.h"
#include "variant.h"

// High level simulation and contact management
struct Simulation : System {
 // Process parameters
 sconst float Gz = -10 * N/kg; // Gravity
 float radius;
 const float targetHeight = radius * 2;
 const float patternRadius = radius - Grain::radius;
 enum Pattern { None, Helix, Cross, Loop };
 sconst string patterns[] {"none", "helix", "radial", "spiral"};
 const Pattern pattern;
 const float linearSpeed = 4 * m/s;
 const float verticalSpeed = 0.2 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 enum ProcessState { Pour, Release, Done, Error };
 //sconst string processStates[] {"pour", "release", "done", "error"};
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = patternRadius;

 // Grain-Bottom
 buffer<uint> oldGrainBottomA;
 buffer<float> oldGrainBottomLocalAx;
 buffer<float> oldGrainBottomLocalAy;
 buffer<float> oldGrainBottomLocalAz;
 buffer<float> oldGrainBottomLocalBx;
 buffer<float> oldGrainBottomLocalBy;
 buffer<float> oldGrainBottomLocalBz;

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

 // Grain-Side
 buffer<uint> oldGrainSideA;
 buffer<float> oldGrainSideLocalAx;
 buffer<float> oldGrainSideLocalAy;
 buffer<float> oldGrainSideLocalAz;
 buffer<float> oldGrainSideLocalBx;
 buffer<float> oldGrainSideLocalBy;
 buffer<float> oldGrainSideLocalBz;

 buffer<uint> grainSideA;
 buffer<float> grainSideLocalAx;
 buffer<float> grainSideLocalAy;
 buffer<float> grainSideLocalAz;
 buffer<float> grainSideLocalBx;
 buffer<float> grainSideLocalBy;
 buffer<float> grainSideLocalBz;

 buffer<float> grainSideFx;
 buffer<float> grainSideFy;
 buffer<float> grainSideFz;
 buffer<float> grainSideTAx;
 buffer<float> grainSideTAy;
 buffer<float> grainSideTAz;

 // Wire-Bottom
 buffer<uint> oldWireBottomA;
 buffer<float> oldWireBottomLocalAx;
 buffer<float> oldWireBottomLocalAy;
 buffer<float> oldWireBottomLocalAz;
 buffer<float> oldWireBottomLocalBx;
 buffer<float> oldWireBottomLocalBy;
 buffer<float> oldWireBottomLocalBz;

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

 Simulation(const Dict& p);
 void domain(vec3& min, vec3& max);
 void step();
 void stepProcess();
  tsc processTime;
 void stepGrain();
  tsc grainTime;
 void stepGrainBottom();
  //tsc grainBottomSearchTime; // TODO: verlet
  uint64 grainBottomFilterTime = 0;
  uint64 grainBottomEvaluateTime = 0;
  tsc grainBottomSumTime;
 void stepGrainSide();
  //tsc grainSideSearchTime; // TODO: verlet
  tsc grainSideFilterTime;
  tsc grainSideEvaluateTime;
  tsc grainSideSumTime;
 void stepGrainGrain();
  tsc grainGrainSearchTime;
  uint64 grainGrainFilterTime = 0;
  uint64 grainGrainEvaluateTime = 0;
  tsc grainGrainSumTime;
 void stepGrainWire();
  uint64 grainWireSearchTime = 0;
  uint64 grainWireFilterTime = 0;
  //tsc grainWireEvaluateTime;
  uint64 grainWireEvaluateTime = 0;
  tsc grainWireSumTime;
  size_t grainWireContactSizeSum;
 void stepGrainIntegration();
  uint64 grainIntegrationTime = 0;
 void stepWire();
  uint64 wireInitializationTime = 0;
 void stepWireTension();
  tsc wireTensionTime;
 void stepWireBendingResistance();
  tsc wireBendingResistanceTime;
 void stepWireBottom();
 //tsc wireBottomSearchTime; // TODO: verlet
  uint64 wireBottomFilterTime = 0;
  tsc wireBottomEvaluateTime;
  tsc wireBottomSumTime;
 void stepWireIntegration();
  uint64 wireIntegrationTime = 0;

#if DEBUG
  void invariant_();
#define invariant invariant_();
#else
#define invariant
#endif
  void profile(const Time& totalTime);
  bool stepProfile(const Time& totalTime);
   Time stepTimeRT;
   tsc stepTime;
};

