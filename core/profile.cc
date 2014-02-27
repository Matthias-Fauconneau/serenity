/// \file profile.cc Profiles executable when linked (need all sources to be compiled with -finstrument-functions)
#include "map.h"
#include "time.h"
#include "trace.h"
inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }
inline float round(float x) { return __builtin_roundf(x); }
template<Type K, Type V, size_t N> struct map {
    K keys[N];
    V values[N];
    size_t size;
    V& operator[](K key) {
        for(uint i: range(size)) if(keys[i] )
    }
};

inline
/// Traces functions to time their execution times and displays statistics on exit
struct Profile {
    struct Frame { void* function; uint64 time; uint64 tsc; };
    Frame stack[32] = {Frame{0,0,rdtsc()}};
    Frame* top = stack;
    struct Function { uint time=0,count=0; bool operator<(const Function& o)const{return time<o.time;} };
    map<void*, Function, 0x1000> profile;

    ~Profile() {
        map<Function, void*> sort;
        uint64 total=0;
        for(auto e: profile) if(e.value.count>1) { sort.insertSortedMulti(e.value, e.key); total+=e.value.time; }
        for(auto e: sort) if(100.f*e.key.time/total>1) {
            Symbol s = findSymbol(e.value);
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
        for(Function& f: profile) {
            profile[top->function];
        f.count++;
        f.time += top->time;
        top--;
        top->tsc = tsc;
    }
};
Profile profile __attribute((init_priority(101)));
extern "C" void __cyg_profile_func_enter(void* function, void*) { if(function) profile.enter(function); }
extern "C" void __cyg_profile_func_exit(void*, void*) { profile.exit(); }
