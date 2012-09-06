#include "map.h"
#include "time.h"

static bool profile = 0;
static int untraced;
struct Profile {
    array<void*> stack;
    array<int> enter;
    map<void*, int> profile;

    Profile() { ::profile=1; }
    ~Profile() {
        ::profile=0;
        map<int, void*> sort;
        for(auto e: profile) if(e.value>0) sort.insertMulti(e.value, e.key);
        for(auto e: sort) log(str(e.key)+"\t"_+findNearestLine(e.value).function);
        log(untraced);
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
} profiler;

extern "C" notrace void __cyg_profile_func_enter(void* function, void*) { if(profile) { profile=0; profiler.trace(function); profile=1; }}
extern "C" notrace void __cyg_profile_func_exit(void*, void*) { if(profile) { profile=0; profiler.trace(0); profile=1; } else untraced++;}
