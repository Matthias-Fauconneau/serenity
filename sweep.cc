#include "simulation.h"

struct ParameterSweep {
 ParameterSweep() {
  for(int subStepCount: {512,1024,2048,4096}) {
    for(size_t wireCapacity: {4096, 8192, 16384, 32768}) {
     for(float frictionCoefficient: {1.f, 1.f/2, 1.f/4, 1.f/8}) {
      for(float initialLoad: {0.1,0.2,0.4,0.8}) {
       for(float wireElasticModulus: {8e6,16e6,32e6,64e6}) {
        for(float winchRate: {500,400,600,700}) {
         const String id = str(subStepCount, frictionCoefficient,
            int(wireElasticModulus/1e6), int(winchRate), wireCapacity, initialLoad);
         if(existsFile(id+".result")) { log("Skipping existing", id); continue; }
         log(id);
         Simulation s{Simulation::Parameters{subStepCount, frictionCoefficient,
             wireElasticModulus, winchRate, wireCapacity, initialLoad},
                      File(id, currentWorkingDirectory(), Flags(WriteOnly|Create))};
         Time time (true);
         while(s.processState < Simulation::Done) {
          s.step();
          if(s.timeStep%(60*s.subStepCount) == 0) log(int(s.timeStep*s.dt));
         }
         if(s.processState != Simulation::Done) {
          log("Failed");
          rename(id, id+".failed");
         } else {
          rename(id, id+".result");
         }
         log(time);
        }
       }
      }
     }
    }
  }
 }
} app;
