#include "process.h"
#include "array.cc"

struct pollfd { int fd; short events, revents; };
extern "C" int poll(pollfd* fds, size_t nfds, int timeout);
constexpr int POLLIN = 1, POLLHUP = 16;

ArrayOfCopyableComparable(Poll*)
static array<Poll*> polls;
ArrayOfCopyable(pollfd)
static array<pollfd> pollfds;
void Poll::registerPoll(pollfd fd) { polls << this; pollfds << fd; }
void Poll::unregisterPoll() { pollfds.removeAt(removeOne(polls, this)); }

int waitEvents() {
    if(!polls.size()) return 0;
    ::poll((pollfd*)pollfds.data(),polls.size(),-1);
    for(uint i=0;i<polls.size();i++) {
        int events = pollfds[i].revents;
        if(events) {
            if(!(events&POLLIN)) warn("!POLLIN"_);
            if(events&POLLHUP) { warn("POLLHUP"_); polls.removeAt(i); pollfds.removeAt(i); i--; continue; }
            polls[i]->event(pollfds[i]);
        }
    }
    return polls.size();
}

extern "C" int fork();
extern "C" int execv(const char* path, char* const argv[]);
Array(CString)
void execute(const string& path, const array<string>& args) {
    array<CString> args0(1+args.size());
    args0 << strz(path);
    for(uint i=0;i<args.size();i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i].data;
    argv[args0.size()]=0;
    int pid = fork();
    if(pid==0) {
        if(!execv(strz(path),(char* const*)argv)) __builtin_abort();
    }
}

extern "C" int setpriority(int which, uint who, int prio);
void setPriority(int priority) { setpriority(0,0,priority); }

#if RUSAGE
#include <sys/resource.h>
int getCPUTime() {
    rusage usage; getrusage(RUSAGE_SELF,&usage);
    return  usage.ru_stime.tv_sec*1000+usage.ru_stime.tv_usec/1000 + //user time in ms
            usage.ru_utime.tv_sec*1000+usage.ru_utime.tv_usec/1000; //kernel time in ms
}
#endif

#if PROCFS
#include "map.h"
#include "file.h"
#include "stream.h"
uint availableMemory() {
    int fd = openFile("/proc/meminfo"_);
    TextBuffer s = ::readUpTo(fd,2048);
    close(fd);
    map<string, uint> info;
    while(s) {
        string key=s.until(":"_); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until("\n"_);
        info.insert(move(key), value);
    }
    return info.at("MemFree"_)+info.at("Inactive"_);
}
#endif

#ifdef PROFILE
Array(void*)

static bool trace = false;
struct Profile {
    array<void*> stack;
    array<int> enter;
    map<void*, int> profile;

    Profile() { ::trace=true; }
    ~Profile() {
        ::trace=false;
        map<int, void*> sort;
        for(auto e: profile) if(e.value>0) sort.insertMulti(e.value, e.key);
        for(auto e: sort) log(str(e.key)+"\t"_+findNearestLine(e.value).function);
    }
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
} profile;

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace) { trace=0; profile.trace(function); trace=1; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace) { trace=0; profile.trace(0); trace=1; } }

#endif
