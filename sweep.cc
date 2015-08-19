#include <sys/prctl.h>
#include "view.h"
#include "sge.h"

/// Returns coordinates occuring in \a points
map<String, array<Variant> > coordinates(ref<Dict> keys) {
 map<String, array<Variant>> allCoordinates;
 for(const Dict& coordinates: keys) for(const auto coordinate: coordinates)
  if(!allCoordinates[copy(coordinate.key)].contains(coordinate.value)) allCoordinates.at(coordinate.key).insertSorted(copy(coordinate.value));
 return allCoordinates;
}

struct ParameterSweep {
 ParameterSweep() {
  prctl(PR_SET_PDEATHSIG, 1/*SIGHUP*/);
  mainThread.setPriority(19);
  //if(!arguments() || arguments().contains("pretend"_)1) {
   array<String> all, missing;
   Dict parameters = parseDict("Rate:100"_);
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
   array<SGEJob> jobs = qstat(0);
   {
    array<char> running, queued;
    for(SGEJob& job: jobs) {
     if(job.state == "running") running.append(" "+job.id);
     if(job.state == "pending") queued.append(" "+job.id);
    }
    if(running) log("Running jobs:", "qdel -f"+running+" &");
    if(queued) log("Queued jobs:", "qdel -f"+queued+" &");
   }
   size_t done = 0, running = 0, queued = 0;
   /*for(float dt: {1e-5}) {
    parameters["TimeStep"__] = String(str(int(round(dt*1e6)))+"µ");*/
   {
    for(string plateSpeed: {0?"8e-5"_:"1e-4"_}) {
     parameters["PlateSpeed"__] = plateSpeed;
     for(float frictionCoefficient: {0.1}) {
      parameters["Friction"__] = frictionCoefficient;
      for(string pattern: ref<string>{"none","helix","cross","loop"}) {
       parameters["Pattern"__] = pattern;
       for(int pressure: {0,80,160,320/*,640*/}) {
        parameters["Pressure"__] = String(str(pressure)+"K"_);
        array<float> radii = copyRef(ref<float>{/*0.02*/0.03/*,0.05*/});
        // Validation
        //if(/*pressure == 80*//*pattern=="none" &&*/ !radii.contains(0.05)) radii.append(0.05);
        for(float radius: radii) {
         //if(pressure == 80 && radius==0.05f && pattern!="none"_) continue; // Validation
         parameters["Radius"__] = radius;
         for(string thickness: ref<string>{"1e-3" /*"5e-3"_,*//*"10e-3"*//*, "20e-3"*/}) {
          parameters["Thickness"__] = thickness;
#if 1
          for(string side: ref<string>{/*"1e9",*/"10e9"/*,"100e9"*/}) {
           parameters["Side"__] = side;
#else
         {
#endif
           for(int seed: {1/*,2,3,4,5,6*/}) {
            parameters["Seed"__] = seed;
            auto add = [&]{
             String id = str(parameters);
             if(arguments().size > 0 && arguments()[0].contains('=')) {
              auto filter = parseDict(arguments()[0]);
              if(!parameters.includes(filter)) return;
             }
             all.append(copyRef(id));
             if(jobs.contains(parseDict(id)/*parameters*/)) {
              const auto& job = jobs.at(jobs.indexOf(parseDict(id)));
              if(job.state == "running") {
               //assert_(job.state == "running");
               //if(!existing.contains(id)) log("Moved");
               running++;
              }
              else {
               assert_(job.state == "pending", job.state, job.id, job.dict, job.elapsed);
               //if(existing.contains(id)) log("Existing");
               queued++;
              }
              jobs.take(jobs.indexOf(parseDict(id)));
              return;
             }
             if(existing.contains(id)
                //&& existsFile(id+".result", "Results"_) && File(id+".result", "Results"_).modifiedTime()/second > currentTime()-24*60*60
                ) {
              //log(id, "Done");
              done++;
              return;
             }
             missing.append(move(id));
            };
            if(pattern == "none") add();
            else {
             for(float wireElasticModulus: {1e8}) {
              parameters["Elasticity"__] = String(str(int(round(wireElasticModulus/1e8)))+"e8");
              for(string wireDensity: {"6%"_,"12%"_}) {
               parameters["Wire"__] = wireDensity;
               if(pattern == "helix") add();
               else {
                for(float angle: {1.2 /*, 2.4*//*PI*(3-sqrt(5.))*/, 3.6}) {
                 parameters["Angle"__] = angle;
                 add();
                }
                parameters.remove("Angle"_);
               }
              }
              parameters.remove("Wire"_);
             }
             parameters.remove("Elasticity"_);
            }
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
    array<char> running, queued;
    size_t runningCount = 0, queuedCount = 0;
    for(SGEJob& job: jobs) {
     if(job.state == "running") { running.append(" "+job.id); runningCount++; }
     if(job.state == "pending") { queued.append(" "+job.id); queuedCount++; }
    }
    if(running) log("Unused running jobs:", "qdel -f"+running+" &");
    if(queued) log("Unused queued jobs:", "qdel -f"+queued+" &");
    log(runningCount+queuedCount, "jobs are not included in the current sweep parameters");
   }

   if(arguments().contains("archive")) {// Archive existing results not in current sweep or too old
    size_t archiveCount = 0, removeCount = 0;
    for(string name: list) {
     if(name=="core"_) { remove("core", "Results"_); continue; }
     string id = name;
     if(endsWith(name, ".result") || endsWith(name, ".working") || find(name, ".o") ||
        endsWith(name, ".wire") || endsWith(name, ".grain") || endsWith(name, ".side"))
      id = section(name,'.',0,-2);
     if(!all.contains(id) ||  File(name, "Results"_).modifiedTime()/second < currentTime()-24*60*60) {
      Folder archive("Archive"_, currentWorkingDirectory(), true);
      //assert_(!existsFile(name, archive), name);
      log(id);
      if(jobs.contains(parseDict(id))) log("Archiving unused results of still running job");
      if(existsFile(name, archive)) removeCount++;
      if(arguments().contains("rename"_)) {
       if(existsFile(name, archive)) remove(name, archive);
       rename("Results"_, name, archive, name);
      }
      archiveCount++;
     }
    }
    if(archiveCount) log(removeCount, "Archive", archiveCount, list.size);
   }

   map<String, array<Variant>> coordinates = ::coordinates(
      apply(all,[](string id){return parseDict(id);}));
   array<String> fixed;
   for(const auto& coordinate: coordinates)
    if(coordinate.value.size==1) fixed.append(copyRef(coordinate.key));

   Random random;
   if(existsFile("serenity/queue.sh")) {
    size_t missingCount = missing.size;
    size_t count = 0;
    while(missing) {
     if(arguments().contains("pretend"_)) {
      Dict shortSet = parseDict(missing.take(random%missing.size));
      for(string dimension: fixed) if(shortSet.contains(dimension)) shortSet.remove(dimension);
      log(shortSet);
      //if(shortSet.contains("Pressure")) shortSet.remove("Pressure");
      //shortSet.remove("Seed");
     }
     else {
      if(arguments().size > 0 && isInteger(arguments()[0]) && count>=(size_t)parseInteger(arguments()[0])) {
       log("Limit", parseInteger(arguments()[0]));
       break;
      }
      if(execute("/bin/sh", {"serenity/queue.sh"_, missing.take(random%missing.size)})) {
       log("Error");
       break;
      }
      count++;
     //log("TEST"); break;
     }
    }
    size_t total = done+running+queued+missingCount;
    log("Done:",done, "Running:",running, "Queued",queued, "Missing", missingCount, "=Total:", total);
   } else {
    array<int> jobs;
    int success = 0;
    while(missing) {
     while(jobs.size >= 4) {
      int pid = wait(); // Waits for any child to terminate
      int status = wait(pid);
      jobs.take(jobs.indexOf(pid));
      if(status) { log("Failed"); goto break2; } // Stops spawning simulation on first failure
      else success++;
     }
     String id = missing.take(random%missing.size);
     jobs.append( execute(cmdline()[0], {id}, false) );
    }
break2:;
    log(success);
   }
  /*} else {
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
   }*/
  }
 } app;
