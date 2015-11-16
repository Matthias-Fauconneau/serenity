#include "variant.h"
#include "xml.h"
#include "file.h"
#include "time.h"

struct SGEJob { String state; Dict dict; String id; float elapsed; };
String str(const SGEJob& o) { return str(o.dict, o.id, o.elapsed); }
bool operator==(const SGEJob& o, const Dict& dict) { return o.dict == dict; }

struct SSH : Poll {
    Stream stdout;
    int pid;
    array<byte> status;

    SSH(ref<string> args, bool log=false) {
        static String hostname = File("/proc/sys/kernel/hostname").readUpTo(256);
        if(hostname=="ifbeval01"_ || !arguments() || !startsWith(arguments()[0],"server-")) {
            if(!which("qstat")) { ::log("Missing qstat"); return; }
            pid = execute(which(args[0]), args.slice(1), false, currentWorkingDirectory(), &stdout);
        } else {
            pid = execute("/usr/bin/ssh",arguments()[0]+args, false, currentWorkingDirectory(), &stdout);
        }
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

array<SGEJob> qstat(int time=0/*, bool running=true*/) {
 static Folder cache {".cache", currentWorkingDirectory(), true};
 string id = "qstat"; //arguments() ? arguments()[0] : hostname;
 if(!existsFile(id, cache) || File(id, cache).modifiedTime() < realTime()-time*60e9) {
  writeFile(id, SSH(ref<string>{"qstat"_,"-u"_,user(),"-xml"_}), cache, true);
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
