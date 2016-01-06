#include "sge.h"

struct ParameterSweep {
 ParameterSweep() {
  array<String> all, missing;
  Dict parameters;
  array<String> existing;
  auto list = Folder("Results", home()).list(Files);
  for(string name: list) {
   existing.append(copyRef(name));
   //if(startsWith(name, "Count=")) remove(name);
  }
  array<SGEJob> jobs = qstat();
  {
   array<char> running, queued;
   size_t runningCount = 0, queuedCount = 0;
   for(SGEJob& job: jobs) {
    if(job.state == "running") { running.append(" "+job.id); runningCount++; }
    if(job.state == "pending") { queued.append(" "+job.id); queuedCount++; }
   }
   if(running) log("Running jobs: ["+str(runningCount)+"]: qdel -f"+running+" &");
   if(queued ) log("Queued jobs:["+str(queuedCount)+"]: qdel -f"+queued+" &");
  }
  size_t done = 0, running = 0, queued = 0;\
  parameters["nDamping"__] = "1"__;
  parameters["mDensity"__] = "1"__;
  for(string dt: {"1"_}) {
   parameters["TimeStep"__] = String(dt+"µ");
   for(string plateSpeed: {/*"1"_,*/"10"_/*,"100"_*/}) {
    parameters["Speed"__] = plateSpeed; // mm/s
    for(int pressure: {80}) {
     parameters["Pressure"__] = String(str(pressure)+"K"_); // Pa
     for(float radius: {25, 50}) {
      parameters["Radius"__] = radius; //mm
      for(string staticFrictionSpeed: {"100"_,"1000"_}) {
       parameters["sfSpeed"__] = staticFrictionSpeed; // mm/s
       for(string staticFrictionLength: {"1µ"_,"10µ"_}) {
        parameters["sfLength"__] = staticFrictionLength; // m
        for(string staticFrictionStiffness: {/*"1K"_,*/"10K"_,"100K"_}) {
         parameters["sfStiffness"__] = staticFrictionStiffness;
         for(string staticFrictionDamping: {"1"_,"10"_,"100"_}) {
          parameters["sfDamping"__] = staticFrictionDamping; // ?
          auto add = [&] {
           String id = str(parameters);
           if(arguments().size > 0 && arguments()[0].contains('=')) {
            auto filter = parseDict(arguments()[0]);
            if(!parameters.includes(filter)) return;
           }
           all.append(copyRef(id));
           if(jobs.contains(parseDict(id))) {
            const auto& job = jobs.at(jobs.indexOf(parseDict(id)));
            if(job.state == "running") running++;
            else queued++;
            jobs.take(jobs.indexOf(parseDict(id)));
            return;
           }
           if(existing.contains(id)) {
            //if(existsFile(id)) { log("Remove", id); remove(id); }
            done++; return;
           }
           missing.append(move(id));
          };
          add();
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
    else if(job.state == "pending") { queued.append(" "+job.id); queuedCount++; }
    else error(job.state);
   }
   if(running) log("Unused running jobs: ["+str(runningCount)+"]: qdel -f"+running+" &");
   if(queued) log("Unused queued jobs: ["+str(queuedCount)+"]: qdel -f"+queued+" &");
   log(runningCount+queuedCount, "jobs are not included in the current sweep parameters");
  }

  //int random=0;
  Random random;
  size_t missingCount = missing.size;
  size_t count = 0;
  while(missing) {
   if(arguments().contains("pretend"_)) log(parseDict(missing.take(random%missing.size)));
   else {
    String parameters = missing.take(random%missing.size);
    String name = replace(parameters,':','=');
    if(execute("/opt/ge2011.pleiades/bin/linux-x64/qsub.orig", {
               "-q", "fast.q@@blade04,fast.q@@blade05",
               "-l", "fq=true",
               "-N", name,
               "-j", "y",
               "-o", "$JOB_NAME.stdout",
               "-b","y",
               "-pe", "omp", "16",
               "~/run", parameters}, true, Folder("Results", home()))) { log("Error"); break; }
    count++;
   }
  }
  size_t total = done+running+queued+missingCount;
  log("Done:",done, "Running:",running, "Queued",queued, "Missing", missingCount, "=Total:", total);
 }
} app;
