#include "process.h"
#include "time.h"
#include "map.h"
#include "interface.h"
#include "window.h"

/// Convenient partial template specialization to automatically copy ref<byte> keys
template<class V> struct map<ref<byte>,V> : map<string,V> {
    V& operator [](ref<byte> key) { return map<string,V>::operator[](string(key)); }
};

struct Monitor : Timer {
    UniformGrid<Text> layout;
    Window window __(&layout,int2(-1,-1),"Monitor"_);
    Folder procfs __("proc"_);
    /// Returns system statistics
    map<ref<byte>,string> stat() {
        string stat = File("stat"_,procfs).readUpTo(4096);
        const ref<byte> keys[]={""_,"user"_, "nice"_, "idle"_};
        array< ref<byte> > fields = split(stat,' ');
        map< ref<byte>, string> stats;
        for(uint i: range(3)) stats[keys[i]]=string(fields[i]);
        return stats;
    }
    /// Returns process statistics
    map<ref<byte>, string> stat(const ref<byte>& pid) {
        const ref<byte> keys[]={"pid"_, "name"_, "state"_, "parent"_, "group"_, "session"_, "tty"_, "tpgid"_, "flags"_, "minflt"_, "cminflt"_, "majflt"_, "cmajflt"_, "utime"_, "stime"_, "cutime"_, "cstime"_, "priority"_, "nice"_, "#threads"_, "itrealvalue"_, "starttime"_, "vsize"_, "rss"_};
        map< ref<byte>, string> stats;
        if(!existsFolder(pid,procfs)) return stats;
        string stat = File("stat"_,Folder(pid,procfs)).readUpTo(4096);
        array< ref<byte> > fields = split(stat,' ');
        for(uint i: range(24)) stats[string(keys[i])]=string(fields[i]);
        return stats;
    }
    map<ref<byte>, string> system;
    map<ref<byte>, map<ref<byte>,string> > process;
    Monitor() { window.localShortcut(Escape).connect(&exit); layout.width=3;  event(); }
    void event() {
        map<ref<byte>, uint> memory;
        for(TextData s = File("/proc/meminfo"_).readUpTo(4096);s;) {
            ref<byte> key=s.until(':'); s.skip();
            uint value=toInteger(s.untilAny(" \n"_)); s.until('\n');
            memory[key]=value;
        }
        map<ref<byte>, string>& o = this->system;
        map<ref<byte>, string> n = stat();
        map<ref<byte>, map<ref<byte>, string> > process;
        for(const string& pid: procfs.list(Folders)) if(isInteger(pid)) process[pid]=stat(pid);
        if(o) {
            /*log("User: "_+dec(toInteger(n["user"_])-toInteger(o["user"_]))+"%"
                "\tNice: "_+dec(toInteger(n["nice"_])-toInteger(o["nice"_]))+"%"
                "\tIdle: "_+dec(toInteger(n["idle"_])-toInteger(o["idle"_]))+"%"
                "\tFree Memory: "_+dec((memory["MemFree"_]+memory["Inactive"_])/1024)+" MB\tDisk Buffer: "_+dec(memory["Active(file)"_]/1024)+" MB"_);
            log("Name\tRSS (MB)\tCPU (%)");*/
            layout.clear();
            layout << string("Name"_) << string("RSS (MB)"_) << string("CPU (%)"_);

            for(string& pid: procfs.list(Folders)) if(isInteger(pid)) {
                map<ref<byte>,string>& o = this->process[pid];
                map<ref<byte>,string>& p = process[pid];
                ref<byte> name = p["name"_].slice(1,p["name"_].size()-2);
                //const ref<byte> states[]={"Running"_, "Sleeping"_, "Waiting for disk"_, "Zombie"_, "Traced/Stopped"_,  "Paging"_};
                //ref<byte> state = states["RSDZTW"_.indexOf(p["state"_][0])];
                int cpu = toInteger(p["utime"_])-toInteger(o["utime"_])+toInteger(p["stime"_])-toInteger(o["stime"_]);
                float rss = toInteger(p["rss"_])*4/1024.f;
                if(p["state"_]=="R"_||rss>=1) {
                    //log(name+"\t"_+ftoa(rss/1024.f,1)+"\t"_+dec(cpu));
                    layout << string(name) << ftoa(rss,1) << dec(cpu);
                }
            }
            window.setSize(int2(-1,-1));
            window.render();
        }
        this->system=move(n); this->process=move(process);
        setAbsolute(currentTime()+1);
    }
} monitor;
