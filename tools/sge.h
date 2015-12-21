#include "variant.h"
#include "xml.h"
#include "time.h"
#include "thread.h"

struct SGEJob { String state; Dict dict; String id; float elapsed; };
String str(const SGEJob& o) { return str(o.dict, o.id, o.elapsed); }
bool operator==(const SGEJob& o, const Dict& dict) { return o.dict == dict; }

buffer<byte> eval(const string path, ref<string> args) {
 Stream stdout;
 /*int pid =*/ execute(path, args, true, currentWorkingDirectory(), &stdout);
 /*array<byte> buffer;
 while(isRunning(pid)) {
  ::buffer<byte> packet = stdout.readUpTo(1<<16);
  if(!packet) break;
  buffer.append(packet);
 }
 assert_(buffer, path, args);
 return ::move(buffer);*/
 return stdout.readUpTo(1<<16);
}

array<SGEJob> qstat() {
 buffer<byte> document
   = eval("/opt/ge2011.pleiades/bin/linux-x64/qstat"_, ref<string>{"-u"_,user(),"-xml"_});
 Element root = parseXML(document); // Unsafe reference to \a document
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
