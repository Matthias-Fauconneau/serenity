#include "map.h"
#include "array.cc"
Array_Copy_Compare_Sort(void*)

#include <time.h>
inline long cpuTime() { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec*1000000+ts.tv_nsec/1000; }

static bool trace = false;
static int untrace;
struct Profile {
    array<void*> stack;
    array<int> enter;
    map<void*, int> profile;

    Profile() { ::trace=true; }
    ~Profile() {
        ::trace=false;
        log(untrace);
        map<int, void*> sort;
        for(auto e: profile) if(e.value>0) sort.insertMulti(e.value, e.key);
        for(auto e: sort) log(str(e.key)+"\t"_+findNearestLine(e.value).function);
    }
    void trace(void* function) {
        if(function) {
            stack << function;
            enter << cpuTime();
        } else {
            assert(stack);
            void* function = stack.pop();
            int time = cpuTime()-enter.pop();
            for(void* e: stack) if(function==e) return; //profile only topmost recursive
            profile[function] += time;
        }
    }
} profile;

extern "C" void __cyg_profile_func_enter(void* function, void*) { if(trace==1) { trace=0; profile.trace(function); trace=1; }else if(trace==0){trace=-1; log(findNearestLine(function).function);trace=0;}}
extern "C" void __cyg_profile_func_exit(void*, void*) { if(trace) { trace=0; profile.trace(0); trace=1; } else untrace++;}
