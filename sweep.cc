#include <sys/prctl.h>
#include "view.h"

struct ParameterSweep {
 ParameterSweep() {
  prctl(PR_SET_PDEATHSIG, 1/*SIGHUP*/);
  mainThread.setPriority(19);
  if(!arguments()) {
   array<String> cases;
   Dict parameters; //= parseDict("Speed:0.04,PlateSpeed:1e-4"_);
   array<String> existing =
     apply(Folder(".").list(Files)
           .filter([](string name){return !endsWith(name, ".result") && !endsWith(name, ".working");}),
     [](string name)->String{ return copyRef(section(name,'.',0,-2)); });
   log(existing);
   for(float dt: {4e-5, 1e-5}) {
    parameters["TimeStep"__] = String(str(int(round(dt*1e6)))+"µ");
    for(float frictionCoefficient: {0.1, 0.3}) {
     parameters["Friction"__] = frictionCoefficient;
     for(string pattern: ref<string>{"none","helix","cross","loop"}) {
      parameters["Pattern"__] = pattern;
      for(float wireElasticModulus: {1e8}) {
       parameters["Elasticity"__] = String(str(int(round(wireElasticModulus/1e8)))+"e8");
       for(float radius: {0.02, 0.03}) {
        parameters["Radius"__] = radius;
        parameters["Height"__] = radius*4;
        for(float winchRate: {100,200}) {
         parameters["Rate"__] = winchRate;
         for(int pressure: {25,50,100,200,400,800,1600,3200}) {
          parameters["Pressure"__] = String(str(pressure)+"K"_);
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
#if 1
   while(cases) {
    //log(cases.take(random%cases.size));
    if(execute("/bin/sh", {"serenity/queue.sh"_, cases.take(random%cases.size)})) {
     log("Error");
     break;
    }
   }
#else
   array<int> jobs;
   int success = 0;
   while(cases) {
    while(jobs.size >= 4) {
     int pid = wait(); // Waits for any child to terminate
     int status = wait(pid);
     jobs.take(jobs.indexOf(pid));
     if(status) { log("Failed"); goto break2; } // Stops spawning simulation on first failure
     else success++;
    }
    String id = cases.take(random%cases.size);
    jobs.append( execute(cmdline()[0], {id}, false) );
   }
   break2:;
   log(success);
#endif
  } else {
   string id = arguments()[0];
   log(id);
   Time time (true);
#if UI
   SimulationView s{parseDict(id),
      File(id+".working", currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate))};
   mainThread.run();
#else
   Simulation s{parseDict(id),
      File(id+".working", currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate))};
   while(s.processState < ProcessState::Done) {
    s.step();
    if(s.timeStep%size_t(1/s.dt) == 0) log(s.info());
   }
#endif
   if(s.processState != ProcessState::Done) {
    log("Failed");
    rename(id+".working", id+".failed", currentWorkingDirectory());
   } else {
    rename(id+".working", id+".result");
   }
   log(time);
  }
 }
} app;
