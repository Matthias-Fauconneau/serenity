/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "map.h"
#include "time.h"
#include "trace.h"

static bool profile = 0;
/// Traces functions to time their execution times and displays statistics on exit
struct Profile {
    array<void*> stack;
    array<uint64> enter;
    map<void*, uint64> profile;

    Profile() { ::profile=1; }
    ~Profile() {
        ::profile=0;
        map<int, void*> sort;
        uint64 total=0;
        for(auto e: profile) if(e.value>0) { sort.insertSortedMulti(e.value, e.key); total+=e.value; }
        for(auto e: sort) if(100*e.key/total>=1) log(str((uint)round(100.f*e.key/total))+"%\t"_+findNearestLine(e.value).function);
    }
};
Profile profiler;

#define notrace __attribute((no_instrument_function))
extern "C" {
void __cyg_profile_func_enter(void* function, void*)  notrace;
void __cyg_profile_func_enter(void* function, void*) {
    if(profile) { profile=0; if(function) { profiler.stack << function; profiler.enter << rdtsc(); } profile=1; }
}
void __cyg_profile_func_exit(void*, void*)  notrace;
void __cyg_profile_func_exit(void*, void*) {
    if(profile) {
        profile=0;
        if(profiler.stack) {
            void* function = profiler.stack.pop();
            uint64 time = rdtsc()-profiler.enter.pop();
            for(void* e: profiler.stack) if(function==e) { profile=1; return; } //profile only topmost recursive
            profiler.profile[function] += time;
        }
        profile=1;
    }
}
}
