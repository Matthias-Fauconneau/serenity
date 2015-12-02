/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "string.h"
struct Symbol { String function; string file; int line=0; };
Symbol findSymbol(void* find);

#if __clang__
#define readCycleCounter  __builtin_readcyclecounter
#else
#define readCycleCounter __builtin_ia32_rdtsc
#endif

/// Traces functions to time their execution times and displays statistics on exit
struct Frame { uint64 tsc=0; void* function=0; uint64 time=0; };
static Frame stack[64] = {Frame{readCycleCounter(), 0, 0}};
static Frame* top = stack;
static constexpr size_t capacity = 0x2000;
struct Entry { void* function=0; uint64 time=0; uint64 count=0; };
Entry entries[capacity];
static size_t size = 0;
static bool tracePaused = false;
bool operator <(const Entry& a, const Entry& b) { return a.time < b.time; }

void profile_reset() { mref<Entry>(entries, capacity).clear(); }
__attribute((destructor(101))) notrace void logProfile() {
	uint64 total = readCycleCounter() - stack[0].tsc;
	tracePaused = true;
	mref<Entry> entries(::entries, ::size);
	sort(entries);
	for(Entry e : entries) {
		if(100*e.time/total==0) continue;
		Symbol s = findSymbol(e.function);
		log(str(100*e.time/total)+"%\t"+str(e.count)+'\t'+s.file+':'+str(s.line)+"     \t"+s.function);
    }
	tracePaused = false;
}

// May not call any non-inline functions to avoid recursions
extern "C" notrace void __cyg_profile_func_enter(void* function, void*) {
	if(tracePaused) return;
    uint64 tsc = readCycleCounter();
    top++;
	assert_(top-stack < 32);
	*top = {tsc, function, 0};
}

extern "C" notrace void __cyg_profile_func_exit(void*, void*) {
	if(tracePaused) return;
    uint64 tsc = readCycleCounter();
    top->time += tsc - top->tsc;
	size_t count = 1;
	for(size_t index = 0;;index++) { // Lookup entry and swap to front
		if(index==size) { // Creates new entry
			size++;
			if(size >= capacity) __builtin_trap();
			// Shift all previous entries to free first slot (erases current entry)
			for(;index > 0;index--) entries[index] = entries[index-1]; // 128bit moves
			break;
		}
        if(entries[index].function == top->function) {
            // Accumulates current entry (64bit add)
            top->time += entries[index].time;
            count += entries[index].count;
            // Shift all previous entries to free first slot (erases current entry)
            for(;index > 0;index--) entries[index] = entries[index-1]; // 128bit moves
            break;
        }
    }
    // Records current entry on first slot
    entries[0] = {top->function, top->time, count};
    top--;
}
