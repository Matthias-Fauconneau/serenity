#include "polyhedra.h"

struct PolyhedraTest : PolyhedraSimulation {
 size_t VF = 0, /*VE = 0,*/ EE = 0;
 PolyhedraTest(Random& random)  : PolyhedraSimulation(random) {
   int64 start = threadCPUTime();
   const size_t timeStepCount = 2048;
   buffer<vec3> positions {timeStepCount * polyhedras.capacity, 0};
   bool record = arguments().contains("record"), test = arguments().contains("test");
   while(t < timeStepCount) {
    step();
    if(record || test) {
     for(const Polyhedra& p: polyhedras) positions.append(p.position);
    }
    for(const Contact& c: contacts) {
     if(c.vertexIndexA != invalid && c.faceIndexB != invalid) VF++;
     //if(c.vertexIndexA != invalid && c.edgeIndexB != invalid) VE++;
     if(c.edgeIndexA != invalid && c.edgeIndexB != invalid) EE++;
    }
   }
   if(record) {
    writeFile("positions", cast<byte>(positions));
   }
   if(test) {
    buffer<vec3> referencePositions = cast<vec3>(readFile("positions"));
    assert(referencePositions.size == positions.size);
    assert_(referencePositions == positions);
   }
   int64 end = threadCPUTime();
    // TODO: +amortize on large particle count (+SIMD, +thread)
   log("VF", VF, /*VE,*/"EE", EE, "Steps", t, "Time", str((end-start)/1e6)+"ms", "Perf", t*1e6/(end-start), "/ms");
   log("contact/step", strD(contactTime, stepTime), "VF", strD(vertexFaceTime, stepTime), strD(edgeEdgeTime, stepTime));
 }
};

struct PolyhedraApp {
 PolyhedraApp() {
  Random random {2375560308, 689823083};
  //Random random;
  for(;;) {
   Random randomStartState = random;
   PolyhedraTest test {random};
   if(test.VF > 1024 /*&& test.VE > 64*/ && test.EE > 1024) {
    log(str(randomStartState.z)+", "+str(randomStartState.w));
    break;
   }
  }
 }
} app;
