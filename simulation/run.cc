#include "simulation.h"
#include "thread.h"
#include "parallel.h"
#include <unistd.h>

__attribute((constructor(101))) void logCompiler() {
#if __INTEL_COMPILER
 log("ICC", simd, ARGS);
#elif __clang__
 log("Clang", simd, ARGS);
#elif __GNUC__
 log("gcc", simd, ARGS);
#else
#error
#endif
#if OPENMP
 log("OpenMP", ::threadCount());
#else
 log("PThread", ::threadCount());
#endif
}

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Simulation {
 Time totalTime;
 SimulationView(const Dict& parameters) : Simulation(parameters) {
  totalTime.start();
  for(;;) {
   if(!run(totalTime)) return;
   if(timeStep%(int(1/(dt*60))/4*32*1) == 0) return;
  }
 }
} app (parameters());
