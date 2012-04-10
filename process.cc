#include "process.h"
#include "map.h"
#include "file.h"
#include "stream.h"

#include <poll.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sched.h>

#include "array.cc"

/// Process

void setPriority(int priority) { setpriority(PRIO_PROCESS,0,priority); }

uint availableMemory() {
    int fd = openFile("/proc/meminfo"_);
    TextBuffer s = ::readUpTo(fd,2048);
    close(fd);
    map<string, uint> info;
    while(s) {
        string key=s.until(":"_); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until("\n"_);
        info[move(key)]=value;
    }
    return info["MemFree"_]+info["Inactive"_];
}

int getCPUTime() {
    rusage usage; getrusage(RUSAGE_SELF,&usage);
    return  usage.ru_stime.tv_sec*1000+usage.ru_stime.tv_usec/1000 + //user time in ms
            usage.ru_utime.tv_sec*1000+usage.ru_utime.tv_usec/1000; //kernel time in ms
}

void execute(const string& path, const array<string>& args) {
    array<string> args0(1+args.size());
    args0 << strz(path);
    for(uint i=0;i<args.size();i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i].data();
    argv[args0.size()]=0;
    pid_t pid = fork();
    if(pid==0) {
        unshare(CLONE_FILES);
        if(!execv(strz(path).data(),(char* const*)argv)) __builtin_abort();
    }
}

/// Poll

static map<Poll*,pollfd> polls __attribute((init_priority(103)));
void Poll::registerPoll(pollfd poll) { polls.insert(this,poll); }
void Poll::unregisterPoll() { if(polls.contains(this)) polls.remove(this); }

int waitEvents() {
    if(!polls.size()) return 0;
    ::poll((pollfd*)polls.values.data(),polls.size(),-1);
    for(int i=0;i<polls.size();i++) {
        int events = polls.values[i].revents;
        if(events) {
            if(!(events&POLLIN)) warn("!POLLIN"_);
            if(events&POLLHUP) { warn("POLLHUP"_); polls.remove(polls.keys[i]); i--; continue; }
            polls.keys[i]->event(polls.values[i]);
        }
    }
    return polls.size();
}

#ifdef PROFILE
/// Profiler

static bool trace = false;
struct Profile {
    array<void*> stack;
    array<int> enter;
    map<void*,int> profile;

    Profile() { trace=true; }
    void trace(void* function) {
        if(function) {
            stack << function;
            enter << getCPUTime();
        } else if(stack) {
            void* function = stack.pop();
            int time = getCPUTime()-enter.pop();
            for(void* e: stack) if(function==e) return; //profile only topmost recursive
            profile[function] += time;
        }
    }
} profile __attribute((init_priority(102)));

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace) { trace=0; profile.trace(function); trace=1; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace) { trace=0; profile.trace(0); trace=1; } }

void logProfile() {
    trace=0;
    for(auto e: profile.profile) {
        if(e.value>40) log(toString(e.value)+"\t"_+findNearestLine(e.key).function);
    }
    trace=1;
}
#endif
