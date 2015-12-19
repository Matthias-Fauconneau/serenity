#include "sge.h"

struct ParameterSweep {
 ParameterSweep() {
  array<String> all, missing;
  Dict parameters;
  array<String> existing;
  auto list = currentWorkingDirectory().list(Files);
  for(string name: list) existing.append(copyRef(name));
  array<SGEJob> jobs = qstat(0);
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
  for(float dt: {1e-6}) {
   parameters["TimeStep"__] = String(str(int(round(dt*1e6)))+"µ");
   for(string plateSpeed: {/*"10"_,*/"20"_/*,"30"_*/}) {
    parameters["Speed"__] = plateSpeed;
    {
     for(float frictionCoefficient: {/*0.1,*/ 0.2/*, 0.3*/}) {
      parameters["Friction"__] = frictionCoefficient; // FIXME: separate Ball-Wire friction coefficient
      for(string pattern: ref<string>{"none"/*,"helix","cross","loop"*/}) {
       parameters["Pattern"__] = pattern;
       for(int pressure: {80}) {
        parameters["Pressure"__] = String(str(pressure)+"K"_);
        for(float radius: {0.25/*, 0.50*/}) {
         parameters["Radius"__] = radius;
         parameters["Count"__] = 3852*4;
         for(float staticFrictionSpeed: {/*0.01,*/ 0.1/*, 1.*/}) {
          parameters["sfSpeed"__] = staticFrictionSpeed;
          for(float staticFrictionLength: {/*0.01e-3,*/ 0.1e-3/*, 1e-3*/}) {
           parameters["sfLength"__] = staticFrictionLength;
           for(float staticFrictionStiffness: {/*1e1,*/ 1e2/*, 1e3*/}) {
            parameters["sfStiffness"__] = String(str(staticFrictionStiffness)+"K");
            for(float staticFrictionDamping: {/*1,*/ 10/*, 100*/}) {
             parameters["sfDamping"__] = staticFrictionDamping;
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
              if(existing.contains(id)) { done++; return; }
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

  Random random;
  size_t missingCount = missing.size;
  size_t count = 0;
  while(missing) {
   if(arguments().contains("pretend"_)) log(parseDict(missing.take(random%missing.size)));
   else {
    String parameters = missing.take(random%missing.size);
    String name = replace(parameters,':','=');
    log("-q", "fast.q@@blade04,fast.q@@blade05",
         "-l", "fq=true",
         //-hard -l h_vmem=4G,vf=4G
         "-N", name,
         "-j", "y",
         "-o", name, //"$JOB_NAME.o$JOB_ID",
         //" -pe", "omp",
         "-b","y", "run", parameters);
    if(execute(which("qsub.orig"), {
               "-q", "fast.q@@blade04,fast.q@@blade05",
               "-l", "fq=true",
               //-hard -l h_vmem=4G,vf=4G
               "-N", name,
               "-j", "y",
               "-o", "$JOB_NAME", //.$JOB_ID",
               //" -pe", "omp",
               "-b","y", "~/run", parameters})) { log("Error"); break; }
    count++;
   }
  }
  size_t total = done+running+queued+missingCount;
  log("Done:",done, "Running:",running, "Queued",queued, "Missing", missingCount, "=Total:", total);
 }
} app;
