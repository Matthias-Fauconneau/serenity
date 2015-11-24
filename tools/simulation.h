#include "system.h"
#include "list.h"
#include "lattice.h"
#include "grid.h"
#include "time.h"
#include "variant.h"

enum Pattern { None, Helix, Cross, Loop };
static string patterns[] {"none", "helix", "cross", "loop"};
enum ProcessState { Pour, Release, Error };
static string processStates[] {"pour", "release", "error"};

struct Simulation : System {
 // Process parameters
 const float radius;
 const float targetHeight = radius;
 const float patternRadius = radius - Grain::radius;
 const Pattern pattern;
 const float linearSpeed = 0.10 * m/s;
 const float verticalSpeed = 0.05 * m/s;
 const float loopAngle = PI*(3-sqrt(5.));

 // Process variables
 ProcessState processState = Pour;
 Random random;
 float currentHeight = Grain::radius;
 float lastAngle = 0, winchAngle = 0, currentRadius = patternRadius;

 // Grain-Plate friction
 buffer<float> grainPlateLocalAx;
 buffer<float> grainPlateLocalAy;
 buffer<float> grainPlateLocalAz;
 buffer<float> grainPlateLocalBx;
 buffer<float> grainPlateLocalBy;
 buffer<float> grainPlateLocalBz;

 // Grain-Side friction
 buffer<float> grainSideLocalAx;
 buffer<float> grainSideLocalAy;
 buffer<float> grainSideLocalAz;
 buffer<float> grainSideLocalBx;
 buffer<float> grainSideLocalBy;
 buffer<float> grainSideLocalBz;

 // Grain - Grain
 float grainGrainGlobalMinD12 = 0;
 uint grainSkipped = 0;

 size_t grainGrainCount = 0;
 static constexpr size_t grainGrain = 12;
 buffer<int> grainGrainA {grain.capacity * grainGrain};
 buffer<int> grainGrainB {grain.capacity * grainGrain};
 // Grain-Grain Friction
 buffer<float> grainGrainLocalAx;
 buffer<float> grainGrainLocalAy;
 buffer<float> grainGrainLocalAz;
 buffer<float> grainGrainLocalBx;
 buffer<float> grainGrainLocalBy;
 buffer<float> grainGrainLocalBz;

 // Grain - Wire
 // TODO: Verlet
 static constexpr size_t grainWire = 64;
 size_t grainWireCount = 0;
 buffer<int> grainWireA {wire.capacity};
 buffer<int> grainWireB {wire.capacity};
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
 bool stepGrainGrain();
 bool stepGrainWire();
 void stepWire();
};

