/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "map.h"
#include "time.h"
#include "trace.h"

/// Traces functions to time their execution times and displays statistics on exit
struct Profile {
    struct Frame { void* function; uint64 time; uint64 tsc; };
    Frame stack[16] = {Frame{0,0,rdtsc()}};
    Frame* top = stack;
    struct Function { uint time=0,count=0; bool operator<(const Function& o)const{return time<o.time;} };
    map<void*, Function> profile;

    ~Profile() {
        map<Function, void*> sort;
        uint64 total=0;
        for(auto e: profile) { sort.insertSortedMulti(e.value, e.key); total+=e.value.time; }
        for(auto e: sort) if(100.f*e.key.time/total>=1) {
            Symbol s = findNearestLine(e.value);
            log(str((uint)round(100.f*e.key.time/total))+"%"_
                +"\t"_+str(e.key.count)
                +"\t"_+s.file+":"_+str(s.line)+"     \t"_+s.function);
        }
    }
    void enter(void* function) {
        uint64 tsc = rdtsc();
        top->time += tsc-top->tsc;
        top++;
        *top = Frame{function,0,tsc};
    }
    void exit() {
        uint64 tsc = rdtsc();
        top->time += tsc-top->tsc;
        Function& f = profile[top->function];
        f.count++;
        f.time += top->time;
        top--;
        top->tsc = tsc;
    }
};
Profile profile;
extern "C" void __cyg_profile_func_enter(void* function, void*) { if(function) profile.enter(function); }
extern "C" void __cyg_profile_func_exit(void*, void*) { profile.exit(); }
