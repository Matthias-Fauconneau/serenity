#include "variant.h"
#include "xml.h"

struct SGEJob { String state; Dict dict; String id; float elapsed; };
String str(const SGEJob& o) { return str(o.dict, o.id, o.elapsed); }
bool operator==(const SGEJob& o, const Dict& dict) { return o.dict == dict; }

array<SGEJob> qstat(int time=0/*, bool running=true*/) {
 static Folder cache {".cache", currentWorkingDirectory(), true};
 static String hostname = File("/proc/sys/kernel/hostname").readUpTo(256);
 string id = arguments() ? arguments()[0] : hostname;
 if(!existsFile(id, cache) || File(id, cache).modifiedTime() < realTime()-time*60e9) {
  Stream stdout;
  int pid;
  if(hostname=="ifbeval01"_ || !arguments() || !startsWith(arguments()[0],"server-"))
   pid = execute(which("qstat"),ref<string>{"-u"_,user(),"-xml"_}, false, currentWorkingDirectory(), &stdout);
  else
   pid = execute("/usr/bin/ssh",ref<string>{arguments()[0],"qstat"_,"-u"_,user(),"-xml"_}, false, currentWorkingDirectory(), &stdout);
  array<byte> status;
  for(;;) {
   auto packet = stdout.readUpTo(1<<16);
   status.append(packet);
   if(!(packet || isRunning(pid))) break;
  }
  writeFile(id, status, cache, true);
 }
 auto document = readFile(id, cache);
 Element root = parseXML(document);
 array<SGEJob> jobs;
 array<String> badJobs;
 for(const Element& rootList: root.children) {
  for(const Element& list: rootList.children) {
   for(const Element& job: list.children) {
    assert_(job["state"_] == "running"_ || job["state"_] == "pending"_, job["state"_]);
    string name = job("JB_name").content;
    string id = job("JB_job_number").content;
    if(startsWith(job("state").content,"E")) {
     log("Error state", name);
     badJobs.append(copyRef(id));
     continue;
    }
    auto dict = parseDict(name);
    //log(dict);
    if(jobs.contains(dict)) {
     log("Duplicate job", id, dict);
     badJobs.append(copyRef(id));
    }
    jobs.append(SGEJob{copyRef(job["state"_]), move(dict), copyRef(id), job["state"_] == "running"_ ?
                       float(currentTime()-parseDate(job("JAT_start_time").content)) : 0});
   }
  }
 }
 if(badJobs) error("qdel -f", str(badJobs," "_,""_), "&");
 return jobs;
}
