/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "map.h"
#include "time.h"
#include "trace.h"

/// Traces functions to time their execution times and displays statistics on exit
struct Profile {
    struct Frame { void* function; uint64 time; uint64 tsc; };
    array<Frame> stack {1,1,Frame{0,0,rdtsc()}};
    map<void*, uint64> profile;

    ~Profile() {
        map<uint64, void*> sort;
        uint64 total=0;
        for(auto e: profile) if(e.value>0) { sort.insertSortedMulti(e.value, e.key); total+=e.value; }
        for(auto e: sort) if(100*e.key/total>=1) log(str((uint)round(100.f*e.key/total))+"%\t"_+findNearestLine(e.value).function);
    }
    void enter(void* function) {
        stack.last().time += rdtsc()-stack.last().tsc;
        stack << Profile::Frame{function,0,rdtsc()};
    }
    void exit() {
        Frame frame = stack.pop();
        frame.time += rdtsc()-frame.tsc;
        for(const Frame& ancestor: stack) if(frame.function==ancestor.function) return; //only topmost recursive
        profile[frame.function] += frame.time;
        if(stack) stack.last().tsc = rdtsc();
    }
};
Profile profile;
extern "C" void __cyg_profile_func_enter(void* function, void*) { if(function) profile.enter(function); }
extern "C" void __cyg_profile_func_exit(void*, void*) { profile.exit(); }
