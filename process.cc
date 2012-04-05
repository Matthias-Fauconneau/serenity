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

#if DEBUG
map<const char*, int> profile;
#endif

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

#ifdef TRACE
bool trace_enable = false;
struct Trace {
    // trace ring buffer
    void** buffer;
    int size=256;
    int index=0;
    // keep call stack leading to beginning of trace ring buffer
    array<void*> stack;

    Trace() { buffer=new void*[size]; clear(buffer,size); }
    void trace(void* function) {
        for(int loopSize=1;loopSize<=16;loopSize++) { //foreach loop size
            int depth=0;
            for(int i=loopSize;i>0;i--) { //find loop
                if(buffer[(index+size-i)%size]/*current*/ != buffer[(index+size-i-loopSize)%size]/*previous iteration*/) {
                    goto mismatch;
                }
                if(buffer[(index+size-i)%size]) depth++; else depth--;
                if(depth<0) goto mismatch;
            }
            if(depth!=0) goto mismatch;
            //found loop, erase repetition
            index = (index+size-loopSize)%size;
            break;
            mismatch: ;
        }
        void* last = buffer[index];
        if(last) stack << last;
        else if(stack.size) stack.removeLast();
        buffer[index++] = function;
        if(index==size) index=0;
    }
    void log() {
        trace_off;
        int depth=0;
        for(;depth<stack.size-1;depth++) {
            Symbol s = findNearestLine(stack[depth]);
            for(int i=0;i<depth;i++) ::log_(' ');
            ::log(s.function);
        }
        for(int i=0;i<size;i++) {
            void* function = buffer[index++];
            if(index==size) index=0;
            if(!function) { depth--; continue; } else depth++;
            Symbol s = findNearestLine(function);
            for(int i=0;i<depth-1;i++) ::log_(' ');
            ::log(s.function);
        }
    }
} trace __attribute((init_priority(102)));

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

void logTrace() { trace.log(); logBacktrace(StackFrame::current()->caller_frame); }
#endif

#ifdef PROFILE
/// Profiler

bool trace_enable = false;
struct Trace {
    array<void*> stack;
    array<int> enter;
    map<void*,int> profile;

    Trace() { trace_enable=true; }
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
} trace __attribute((init_priority(102)));

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

void logProfile() {
    trace_off;
    for(auto e: trace.profile) {
        if(e.value>40) log(toString(e.value)+"\t"_+findNearestLine(e.key).function);
    }
    trace_on;
}
#endif
