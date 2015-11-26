#include "system.h"
#include "list.h"
#include "lattice.h"
#include "grid.h"
#include "time.h"
#include "variant.h"

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Running, Done, Error };
static string processStates[] {"running", "done", "error"};

// High level simulation and contact management
struct Simulation : System {
 // Process parameters
 sconst float Gz = -10 * N/kg; // Gravity
 const float radius;
 const float targetHeight = radius;
 const float patternRadius = radius - Grain::radius;
 const Pattern pattern;
 const float linearSpeed = 1 * m/s;
 const float verticalSpeed = 0.1 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 ProcessState processState = Running;
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
 float grainGrainGlobalMinD12 = 0;
 uint grainSkipped = 0;

 //size_t grainGrainCount = 0;
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
 // TODO: Verlet
 buffer<uint> grainWireA;
 buffer<uint> grainWireB;
 // Grain-Wire Friction
 buffer<float> grainWireLocalAx;
 buffer<float> grainWireLocalAy;
 buffer<float> grainWireLocalAz;
 buffer<float> grainWireLocalBx;
 buffer<float> grainWireLocalBy;
 buffer<float> grainWireLocalBz;

 struct Force { vec3 origin, force; };
 array<Force> forces;

 Simulation(const Dict& p);

 bool domain(vec3& min, vec3& max);

 unique<Lattice<uint16> > generateLattice(const Mass& vertices, float radius);

 unique<Grid> generateGrid(const Mass& vertices, float length);

 bool step();

 void stepProcess();
 void stepGrain();
 bool stepGrainBottom();
 bool stepGrainSide();
 bool stepGrainGrain();
 bool stepGrainWire();
 void stepWire();
 void stepWireTension();
 bool stepWireBottom();
};

