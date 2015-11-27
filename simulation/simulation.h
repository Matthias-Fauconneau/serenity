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
 const float targetHeight = radius;
 const float patternRadius = radius - Grain::radius;
 enum Pattern { None, Helix, Cross, Loop };
 sconst string patterns[] {"none", "helix", "radial", "spiral"};
 const Pattern pattern;
 const float linearSpeed = 5 * m/s;
 const float verticalSpeed = 0.1 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 enum ProcessState { Pour, Release, Done, Error };
 //sconst string processStates[] {"pour", "release", "done", "error"};
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = patternRadius;

 // Grain-Bottom friction
 buffer<uint> grainBottomA;
 buffer<float> grainBottomLocalAx;
 buffer<float> grainBottomLocalAy;
 buffer<float> grainBottomLocalAz;
 buffer<float> grainBottomLocalBx;
 buffer<float> grainBottomLocalBy;
 buffer<float> grainBottomLocalBz;

 // Wire-Bottom friction
 buffer<uint> wireBottomA;
 buffer<float> wireBottomLocalAx;
 buffer<float> wireBottomLocalAy;
 buffer<float> wireBottomLocalAz;
 buffer<float> wireBottomLocalBx;
 buffer<float> wireBottomLocalBy;
 buffer<float> wireBottomLocalBz;

 // Grain-Side friction
 buffer<uint> grainSideA;
 buffer<float> grainSideLocalAx;
 buffer<float> grainSideLocalAy;
 buffer<float> grainSideLocalAz;
 buffer<float> grainSideLocalBx;
 buffer<float> grainSideLocalBy;
 buffer<float> grainSideLocalBz;

 // Grain - Grain
 float grainGrainGlobalMinD = 0;
 uint grainGrainSkipped = 0;
 buffer<uint> grainGrainA ;
 buffer<uint> grainGrainB;
 // Grain-Grain Friction
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 // Grain - Wire
 float grainWireGlobalMinD = 0;
 uint grainWireSkipped = 0;
 buffer<uint> grainWireA;
 buffer<uint> grainWireB;
 // Grain-Wire Friction
 buffer<float> grainWireLocalAx;
 buffer<float> grainWireLocalAy;
 buffer<float> grainWireLocalAz;
 buffer<float> grainWireLocalBx;
 buffer<float> grainWireLocalBy;
 buffer<float> grainWireLocalBz;

 Simulation(const Dict& p);
 void domain(vec3& min, vec3& max);
 bool step();
 tsc processTime;
 void stepProcess();
 tsc grainTime;
 void stepGrain();
 tsc grainBottomTime;
 bool stepGrainBottom();
 tsc grainSideTime;
 bool stepGrainSide();
 tsc grainGrainTime;
 void stepGrainGrain();
 tsc grainWireTime;
 void stepGrainWire();
 tsc wireTime;
 void stepWire();
 tsc wireTensionTime;
 void stepWireTension();
 tsc wireBottomTime;
 void stepWireBottom();
};

