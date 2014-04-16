/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "trace.h"

#if __GNUC__
inline uint64 __builtin_readcyclecounter() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }
#endif
#define readCycleCounter  __builtin_readcyclecounter

/// Traces functions to time their execution times and displays statistics on exit
static uint64 tsc = readCycleCounter();
struct Frame { uint64 tsc; void* function; uint time, count; };
static Frame stack[32] = {Frame{readCycleCounter(),0,0,1}};
static Frame* top = stack;
static constexpr size_t capacity = 0x1000;
struct Entry { void* function; uint time, count; };
static Entry entries[capacity];
static size_t size = 0;

void __attribute((destructor)) logProfile() notrace;
void __attribute((destructor)) logProfile() {
    uint total = readCycleCounter()-tsc;
    for(uint index=0; index<size; index++) for(uint i=index; i > 0 && entries[i].time > entries[i-1].time;i--) swap(entries[i], entries[i-1]); // In-place insertion sort
    for(uint i=0; i<size; i++) {
        if(100*entries[i].time/total==0) break;
        Symbol s = findSymbol(entries[i].function);
        log(str(100*entries[i].time/total)+"%"_+"\t"_+str(entries[i].count)+"\t"_+s.file+":"_+str(s.line)+"     \t"_+s.function);
    }
}

// May not call any non-inline functions to avoid recursions
extern "C" void __cyg_profile_func_enter(void* function, void*) notrace;
extern "C" void __cyg_profile_func_enter(void* function, void*) {
    uint64 tsc = readCycleCounter();
    top->time += tsc - top->tsc;
    top++;
    *top = {tsc,function,0,1};
}

extern "C" void __cyg_profile_func_exit(void*, void*) notrace;
extern "C" void __cyg_profile_func_exit(void*, void*) {
    uint64 tsc = readCycleCounter();
    top->time += tsc - top->tsc;
    for(uint index = 0;;index++) { // Lookup entry and swap to front
        if(entries[index].function == top->function) {
            // Accumulates current entry (64bit add)
            top->time += entries[index].time;
            top->count += entries[index].count;
            // Shift all previous entries to free first slot (erases current entry)
            for(;index > 0;index--) entries[index] = entries[index-1]; // 128bit moves
            break;
        }
        if(index==size) { // Creates new entry
            size++;
            assert(size < capacity);
            // Shift all previous entries to free first slot (erases current entry)
            for(;index > 0;index--) entries[index] = entries[index-1]; // 128bit moves
            break;
        }
    }
    // Records current entry on first slot
    entries[0] = {top->function, top->time, top->count}; // 128bit move
    top--;
    top->tsc = tsc;
}
