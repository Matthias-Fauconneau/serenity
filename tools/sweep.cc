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
  size_t done = 0, running = 0, queued = 0;
  for(string dt: {"0.05"_,"0.10"_}) {
   parameters["TimeStep"__] = dt;
   for(string plateSpeed: {"10"_/*,"40"_*/}) {
    parameters["Speed"__] = plateSpeed; // mm/s
    for(int pressure: {0,/*10,*/20,/*30,*/40,/*50,*/60,/*70,*/80}) {
     parameters["Pressure"__] = String(str(pressure)+"K"_); // Pa
     for(float radius: {30,40}) {
      parameters["Radius"__] = radius; //mm
      for(float linearSpeed: {20,40}) {
       parameters["linearSpeed"__] = linearSpeed; // m/s
       for(string staticFrictionSpeed: {"20"_,/*"40"_,"80"_*/}) {
        parameters["sfSpeed"__] = staticFrictionSpeed; // mm/s
        for(string staticFrictionLength: {"0.2"_/*,"0.4"_*/}) {
         parameters["sfLength"__] = staticFrictionLength; // mm
         for(string staticFrictionStiffness: {"2K"_/*,"4K"_*/}) {
          parameters["sfStiffness"__] = staticFrictionStiffness;
          for(string staticFrictionDamping: {"1"_}) {
           parameters["sfDamping"__] = staticFrictionDamping; // N/(m/s)*/
           for(string grainShearModulus: {"800"_}) {
            parameters["grainShearModulus"__] = grainShearModulus; // MPa
            for(string pattern: ref<string>{"none","helix","spiral","radial"}) {
             parameters["Pattern"__] = pattern;
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
  array<SGEHost> hosts = qhost();
  while(missing) {
#if 0
   int threadCount = 0;
   for(int N: {1}) {
    for(SGEHost& host: hosts) if(host.jobCount+N<host.slotCount)) {
     host.jobCount+=N;
     log(host);
     threadCount = N;
     goto break2_;
    }
   }
   /*else*/ { log(hosts); if(0) { log("Not all jobs submitted"); break; } else log("Queuing"); }
   break2_:;
   assert_(threadCount);
#else
   int threadCount = 1;
#endif
   if(arguments().contains("pretend"_)) log(parseDict(missing.take(random%missing.size)));
   else {
    String parameters = missing.take(random%missing.size);
    String name = replace(parameters,':','=');
    Folder folder("Results", home());
    if(existsFile(name+".stdout",folder)) remove(name+".stdout",folder);
    if(execute("/opt/ge2011.pleiades/bin/linux-x64/qsub.orig", {
               "-q", "fast.q@@blade04,fast.q@@blade05",
               "-l", "fq=true",
               "-N", name,
               "-j", "y",
               "-o", "$JOB_NAME.stdout",
               "-b","y",
               "-pe", "omp", str(threadCount),
               "~/run", parameters}, true, folder)) { log("Error"); break; }
    count++;
   }
  }
  size_t total = done+running+queued+missingCount;
  log("Done:",done, "Running:",running, "Queued",queued, "Missing", missingCount, "=Total:", total);
 }
} app;
