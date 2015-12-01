#include "simulation.h"

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Simulation {
 Time totalTime, stepTime;
 tsc stepTimeTSC;

 SimulationView(const Dict& parameters) : Simulation(parameters) {
  totalTime.start();
  for(;;) if(!stepProfile(totalTime)) return;
 }
} app (parameters());
