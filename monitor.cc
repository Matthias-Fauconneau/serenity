#include "process.h"
#include "time.h"
#include "map.h"
#include "interface.h"
#include "window.h"

/// Convenient partial template specialization to automatically copy ref<byte> keys
template<Type V> struct map<ref<byte>,V> : map<string,V> {
    V& operator [](ref<byte> key) { return map<string,V>::operator[](string(key)); }
};

struct Monitor : Timer {
    UniformGrid<Text> coreList __(2);
    UniformGrid<Text> processList __(3);
    VBox layout; //__(&coreList,&processList);
    Window window __(&layout,int2(-1,-1),"Monitor"_);
    Folder procfs __("proc"_);
    Folder coretemp __("sys/bus/platform/drivers/coretemp/coretemp.0"_);

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
    Monitor() { window.localShortcut(Escape).connect(&exit); layout<<&coreList<<&processList; event(); }
    void event() {
        // Thermal monitor
        coreList.clear();
        for(int i: range(1,1+coretemp.list(Files).size/5)) {
            string label = readFile(string("temp"_+dec(i)+"_label"_),coretemp);
            int input = toInteger(readFile(string("temp"_+dec(i)+"_input"_),coretemp))/1000;
            if(input>89) { window.show(); log(trim(label), dec(input)+"°C"_); }
            coreList << move(label)  << string(dec(input)+"°C"_);
        }

        // Process monitor
        map<ref<byte>, uint> memory;
        for(TextData s = File("/proc/meminfo"_).readUpTo(4096);s;) {
            ref<byte> key=s.until(':'); s.skip();
            uint value=toInteger(s.untilAny(" \n"_)); s.line();
            memory[key]=value;
        }
        map<ref<byte>, string>& o = this->system;
        map<ref<byte>, string> n = stat();
        map<ref<byte>, map<ref<byte>, string> > process;
        for(const string& pid: procfs.list(Folders)) if(isInteger(pid)) process[pid]=stat(pid);
        if(o) {
            processList.clear();
            processList << string("Name"_) << string("RSS (MB)"_) << string("CPU (%)"_);
            for(string& pid: procfs.list(Folders)) if(isInteger(pid)) {
                map<ref<byte>,string>& o = this->process[pid];
                map<ref<byte>,string>& p = process[pid];
                ref<byte> name = p["name"_].slice(1,p["name"_].size-2);
                int cpu = toInteger(p["utime"_])-toInteger(o["utime"_])+toInteger(p["stime"_])-toInteger(o["stime"_]);
                float rss = toInteger(p["rss"_])*4/1024.f;
                if(p["state"_]=="R"_||rss>=2) processList << string(name) << ftoa(rss,1) << dec(cpu);
            }

            window.setSize(int2(-1,-1));
            window.render();
        }
        this->system=move(n); this->process=move(process);
        setAbsolute(currentTime()+1);
    }
} application;
