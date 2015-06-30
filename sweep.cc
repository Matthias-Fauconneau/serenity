#include "simulation.h"
#include <sys/prctl.h>

struct ParameterSweep {
 ParameterSweep() {
  prctl(PR_SET_PDEATHSIG, 1/*SIGHUP*/);
  mainThread.setPriority(19);
  if(!arguments()) {
   array<String> cases {4*4*4*4*4*4*4}; // 16K
   Dict parameters;
   array<String> existing =
     apply(Folder(".").list(Files)
           .filter([](string name){return !endsWith(name, ".result");}),
     [](string name)->String{
       return copyRef(name.slice(0, name.size-".result"_.size));
   });
   for(int subStepCount: {512,1024,2048,4096}) {
    parameters["Substep count"__] = subStepCount;
    for(float frictionCoefficient: {1.f, 1.f/2, 1.f/4, 1.f/8}) {
     parameters["Friction coefficient"__] = frictionCoefficient;
     for(float initialLoad: {0.1,0.2,0.4,0.8}) {
      parameters["Initial load"__] = initialLoad;
      for(float wireElasticModulus: {8e6,16e6,32e6,64e6}) {
       parameters["Wire elastic modulus"__] = wireElasticModulus;
       for(float height: {0.1, 0.2, 0.4, 0.8}) {
        parameters["Height"__] = height;
        for(float radius: {0.1, 0.2, 0.4, 0.8}) {
         parameters["Radius"__] = radius;
         for(float winchRate: {500,400,600,700}) {
          parameters["Winch rate"__] = winchRate;
          String id = str(parameters);
          if(existing.contains(id)) { log("Skipping existing", id); continue; }
          cases.append(move(id));
         }
        }
       }
      }
     }
    }
   }
   Random random;
   array<int> jobs;
   while(cases) {
    while(jobs.size >= 7) {
     int pid = wait(); // Waits for any child to terminate
     int status = wait(pid);
     jobs.take(jobs.indexOf(pid));
     if(status) { log("Failed"); break; } // Stops spawning simulation on first failure
    }
    String id = cases.take(random%cases.size);
    jobs.append( execute(cmdline()[0], {id}, false) );
   }
  } else {
   string id = arguments()[0];
   log(id);
   Simulation s{parseDict(id),
      File(id+".working", currentWorkingDirectory(), Flags(WriteOnly|Create))};
   Time time (true);
   while(s.processState < Simulation::Done) {
    s.step();
    if(s.timeStep%(60*s.subStepCount) == 0) log(int(s.timeStep*s.dt));
   }
   if(s.processState != Simulation::Done) {
    log("Failed");
    rename(id+".working", id+".failed");
    //requestTermination(-1);1
   } else {
    rename(id+".working", id+".result");
   }
   log(time);
  }
 }
} app;
