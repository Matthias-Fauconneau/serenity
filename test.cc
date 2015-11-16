#include "polyhedra.h"

struct PolyhedraTest : PolyhedraSimulation {
  PolyhedraTest()  {
   int64 start = threadCPUTime();
   const size_t timeStepCount = 2048;
   buffer<vec3> positions {timeStepCount * polyhedras.capacity, 0};
   bool record = arguments().contains("record"), test = arguments().contains("test");
   while(t < timeStepCount) {
    step();
    if(record || test)
     for(const Polyhedra& p: polyhedras) positions.append(p.position);
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
   log(t, str((end-start)/1e6)+"ms", t*1e6/(end-start), "/ms"); // TODO: only intersection time, +amortize on large particle count (+SIMD, +thread)
 }
} test;
