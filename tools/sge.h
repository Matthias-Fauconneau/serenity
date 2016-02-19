#include "variant.h"
#include "xml.h"
#include "time.h"
#include "thread.h"

struct SSH : Poll {
 Stream stdout;
 int pid;
 array<byte> status;

 SSH(ref<string> args, bool log=false) {
  pid = execute("/opt/ge2011.pleiades/bin/linux-x64/qsub.orig", args.slice(1), false, currentWorkingDirectory(), &stdout);
  if(log) { fd=stdout.fd; registerPoll(); }
 }
 void event() {
  read(false, true);
 }
 string read(bool wait=false, bool log=false) {
  if(!stdout) return {};
  for(;;) {
   auto packet = stdout.readUpTo(1<<16);
   status.append(packet);
   if(log && packet) ::log(packet);
   if(!wait || !(packet || isRunning(pid))) { if(log) ::log("Done"); break; }
  }
  return status;
 }
 operator string() { return read(true, false); }
};

buffer<byte> eval(const string path, ref<string> args) {
 Stream stdout; execute(path, args, true, currentWorkingDirectory(), &stdout);
 return stdout.readUpTo(1<<16);
}

struct SGEJob { String state; Dict dict; String id; float elapsed; };
String str(const SGEJob& o) { return str(o.dict, o.id, o.elapsed); }
bool operator==(const SGEJob& o, const Dict& dict) { return o.dict == dict; }

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

struct SGEHost { String name; int jobCount=0, slotCount=0; float load=0; };
String str(const SGEHost& o) { return str(o.name, str(o.jobCount)+"/"+str(o.slotCount)); }

array<SGEHost> qhost() {
 TextData s (eval("/opt/ge2011.pleiades/bin/linux-x64/qhost"_, ref<string>{"-j"_,"-h",
"ifbn043,ifbn044,ifbn045,ifbn046,ifbn047,ifbn048,ifbn049,ifbn050,ifbn051,ifbn052,ifbn053,ifbn054,ifbn055"_}
                              ));
 array<SGEHost> hosts;
 s.line(); s.line(); s.line();
 while(s) {
  SGEHost host;
  host.name = copyRef(s.until(' ')); s.whileAny(' ');
  s.until(' '); s.whileAny(' ');
  host.slotCount = s.integer(); s.whileAny(' ');
  host.load = s.match("-")?0:s.decimal(); s.line();
  for(;s.match(' ');s.line()) {
   s.whileAny(' ');
   if(s.match("job-ID") || s.match("------")) continue;
   else if(s.isInteger() || s.match("normal.q@i"_) || s.match("fast.q@i"_)) host.jobCount++;
   else error(s.line());
  }
  hosts.append(::move(host));
 }
 return hosts;
}

