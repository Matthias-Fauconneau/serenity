#include "simulation.h"

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
