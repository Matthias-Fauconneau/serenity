#include "simulation.h"

__attribute((constructor)) void logCompiler() {
#if __INTEL_COMPILER
 log("ICC");
#elif __clang__
 log("Clang");
#elif __GNUC__
 log("gcc");
#else
#error
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
   if(timeStep%(60*size_t(1/(dt*60))) == 0) return;
  }
 }
} app (parameters());
