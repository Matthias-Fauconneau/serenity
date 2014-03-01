/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "trace.h"

inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }

inline float round(float x) { return __builtin_roundf(x); }

/// Traces functions to time their execution times and displays statistics on exit
struct Profile {
    uint64 tsc = rdtsc();

    struct Frame { uint64 tsc; void* function; uint time; };
    Frame stack[32] = {Frame{rdtsc(),0,0}};
    Frame* top = stack;

    static constexpr size_t capacity = 0x1000;
    void* functions[capacity];
    uint times[capacity];
    uint counts[capacity];
    size_t size = 0;

    ~Profile() {
        uint total = rdtsc()-tsc;
        for(uint i=0; i<size; i++) { // in-place insertion sort
            uint index = i;
            while(index > 0 && times[index] > times[index-1]) {
                swap(functions[index], functions[index-1]);
                swap(times[index], times[index-1]);
                swap(counts[index], counts[index-1]);
                index--;
            }
        }
        for(uint i=0; i<size; i++) {
            if(100*times[i]/total==0) break;
            Symbol s = findSymbol(functions[i]);
            log(str(100*times[i]/total)+"%"_+"\t"_+str(counts[i])+"\t"_+s.file+":"_+str(s.line)+"     \t"_+s.function);
        }
    }
};
Profile profile __attribute((init_priority(101)));
// May not call any non-inline functions to avoid recursions
extern "C" void __cyg_profile_func_enter(void* function, void*) {
    uint64 tsc = rdtsc();
    profile.top->time += tsc - profile.top->tsc;
    profile.top++;
    *profile.top = Profile::Frame{profile.tsc,function,0};
}
extern "C" void __cyg_profile_func_exit(void*, void*) {
    uint64 tsc = rdtsc();
    profile.top->time += tsc - profile.top->tsc;
    uint index = 0;
    for(;;) {
        if(index==profile.size) { // Creates new entry
            profile.size++;
            assert(size < capacity);
            profile.functions[index] = profile.top->function;
            profile.times[index] = profile.top->time;
            profile.counts[index] = 1;
            break;
        }
        if(profile.functions[index] == profile.top->function) { // Updates existing entry
            profile.times[index] += profile.top->time;
            profile.counts[index]++;
            // Keeps profile sorted by call counts for faster search (| binary search on address ?)
            while(index > 0 && profile.counts[index] > profile.counts[index-1]) {
                swap(profile.functions[index], profile.functions[index-1]);
                swap(profile.times[index], profile.times[index-1]);
                swap(profile.counts[index], profile.counts[index-1]);
                index--;
            }
            break;
        }
        index++;
    }
    profile.top--;
    profile.top->tsc = tsc;
}
