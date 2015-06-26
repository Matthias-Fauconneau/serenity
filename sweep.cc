#include "simulation.h"
#include <sys/prctl.h>

struct ParameterSweep {
 ParameterSweep() {
  prctl(PR_SET_PDEATHSIG, 1/*SIGHUP*/);
  mainThread.setPriority(19);
  if(!arguments()) {
   array<String> cases;
   Dict parameters;
   for(int subStepCount: {512,1024,2048,4096}) {
    parameters["subStepCount"__] = subStepCount;
    for(float frictionCoefficient: {1.f, 1.f/2, 1.f/4, 1.f/8}) {
     parameters["frictionCoefficient"__] = frictionCoefficient;
     for(float initialLoad: {0.1,0.2,0.4,0.8}) {
      parameters["initialLoad"__] = initialLoad;
      for(float wireElasticModulus: {8e6,16e6,32e6,64e6}) {
       parameters["wireElasticModulus"__] = wireElasticModulus;
       for(float height: {0.1, 0.2, 0.4, 0.8}) {
        parameters["height"__] = height;
        for(float radius: {0.1, 0.2, 0.4, 0.8}) {
         parameters["radius"__] = radius;
         for(float winchRate: {500,400,600,700}) {
          parameters["winchRate"__] = winchRate;
          String id = str(parameters);
          if(existsFile(id+".result")) { log("Skipping existing", id); continue; }
          cases.append(move(id));
         }
        }
       }
      }
     }
    }
   }
   Random random;
   while(cases) {
    array<int> jobs;
    while(jobs.size >= 7) {
     int pid = wait(); // Waits for any child to terminate
     wait(pid);
     jobs.take(jobs.indexOf(pid));
    }
    jobs.append( execute(cmdline()[0], {cases.take(random%cases.size)}, false) );
   }
  } else {
   string id = arguments()[0];
   log(id);
   Simulation s{parseDict(id),
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
} app;
