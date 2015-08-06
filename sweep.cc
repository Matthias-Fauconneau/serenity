#include <sys/prctl.h>
#include "view.h"
#include "sge.h"

struct ParameterSweep {
 ParameterSweep() {
  prctl(PR_SET_PDEATHSIG, 1/*SIGHUP*/);
  mainThread.setPriority(19);
  if(!arguments() || arguments().contains("pretend"_)) {
   array<String> cases;
   Dict parameters = parseDict("Rate:100,PlateSpeed:1e-4"_);
   /*array<String> existing =
     apply(Folder("Results").list(Files);
           .filter([](string name){return !endsWith(name, ".result") && !endsWith(name, ".working");}),
     [](string name)->String{ return copyRef(section(name,'.',0,-2)); });*/
   array<String> existing;
   auto list = Folder("Results").list(Files);
   for(string name: list) {
    if(endsWith(name, ".result") || endsWith(name, ".working"))
     name = section(name,'.',0,-2);
    existing.append(copyRef(name));
   }
   //if(existing) log(existing);
   array<SGEJob> jobs = qstat(0);
   log(jobs);
   size_t done = 0, running = 0, queued = 0;
   for(float dt: {/*2e-5,*/ 1e-5}) {
    parameters["TimeStep"__] = String(str(int(round(dt*1e6)))+"µ");
    for(float frictionCoefficient: {0.1/*, 0.3*/}) {
     parameters["Friction"__] = frictionCoefficient;
     for(string pattern: ref<string>{"none","helix","cross","loop"}) {
      parameters["Pattern"__] = pattern;
      for(int pressure: {/*25,50,*/100,200,400,800/*,1600*//*,3200*/}) {
       parameters["Pressure"__] = String(str(pressure)+"K"_);
       for(float radius: {0.02,0.03}) {
        parameters["Radius"__] = radius;
        parameters["Height"__] = radius*4;
        for(int seed: {1,2/*,3,4*/}) {
         parameters["Seed"__] = seed;
         auto add = [&]{
          String id = str(parameters);
          //assert_(parseDict(id)==parameters, parseDict(id), parameters);
          if(jobs.contains(parseDict(id)/*parameters*/)) {
           const auto& job = jobs.at(jobs.indexOf(parseDict(id)));
           if(existing.contains(id)) {
            log(id, "Run");
            assert_(job.state == "running");
            running++;
           }
           else {
            log(id, "Queue");
            assert_(job.state == "pending", job.state, job.id, job.dict, job.elapsed);
            queued++;
           }
           jobs.take(jobs.indexOf(parseDict(id)));
           return;
          }
          if(existing.contains(id)) {
           log(id, "Done"); done++;
           return;
          }
          cases.append(move(id));
         };
         if(pattern == "none") add();
         else for(float wireElasticModulus: {1e8}) {
          parameters["Elasticity"__] = String(str(int(round(wireElasticModulus/1e8)))+"e8");
          for(string wireDensity: {"2%"_,/*"3%"_,*/"4%"_,/*"6%"_*//*,"12%"_*/}) {
           parameters["Wire"__] = wireDensity;
           if(pattern == "helix") add();
           else for(float angle: {1.2, 2.4/*PI*(3-sqrt(5.))*/, 3.6}) {
            parameters["Angle"__] = angle;
            add();
           }
          }
         }
        }
       }
      }
     }
    }
   }
   if(jobs) {
    log("Unused jobs:");
    for(SGEJob& job: jobs) log(job.id, job.dict, job.elapsed);
    error(jobs.size, "unused jobs");
   }
   Random random;
   if(existsFile("serenity/queue.sh")) {
    size_t missing = cases.size;
    size_t total = done+running+queued+missing;
    log("Done:",done, "Running:",running, "Queued",queued, "Missing",missing, "=Total:", total);
    while(cases) {
     if(arguments().contains("pretend"_)) log(cases.take(random%cases.size));
     else if(execute("/bin/sh", {"serenity/queue.sh"_, cases.take(random%cases.size)})) {
      log("Error");
      break;
     }
     //log("TEST"); break;
    }
    log("Done:",done, "Running:",running, "Queued",queued, "Missing",missing, "=Total:", total);
   } else {
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
   }
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
