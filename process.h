#pragma once
#include "string.h"
#include "map.h"
struct pollfd;

extern "C" char* getenv(const char* key);

/// Application should be inherited to compile an application into an executable
struct Application {
    Application();
    /// Initializes using the given command line \a args
    virtual void start(array<string>&& /*args*/) {}
    /// Flag to exit event loop and return from main()
    bool running=true;
    /// Set running flag to false so as to quit the application when returning to the event loop.
    /// \note Use this method for normal termination. \a exit doesn't destruct stack allocated objects.
    void quit() { running=false; }
};

/// Poll is an interface for objects needing to participate in event handling
struct Poll {
    /// Add this to the process-wide event loop
    void registerPoll(pollfd);
    /// Callback on new events
    virtual void event(pollfd p) =0;
};

/// Set process CPU scheduling priority (-20 high priority, 19 low priority)
void setPriority(int priority);
/// Execute binary at \a path with command line arguments \a args
void execute(const string& path, const array<string>& args=array<string>());

/// Returns CPU time in milliseconds consumed since start of process
int getCPUTime();

/// Times \a statements and add to the process CPU usage profile
/// \note use getCPUTime to profile kernel time and avoid other interference from other processes (adapted to longer tasks)
extern map<const char*, int> profile;
#define profile(name, statements ) { int start=getCPUTime(); statements; profile[#name]+=getCPUTime()-start; }

/// Log the corresponding assembly the first time \a statements is executed
void disasm(array<ubyte> code);
#define disasm( statements ) { \
    begin: statements; end: \
    static bool once=false; if(!once) disasm(array<ubyte>((ubyte*)&&begin,(ubyte*)&&end)), once=true; \
}

/// Returns the number of cycles used to execute \a statements
inline uint64 rdtsc() {
    asm volatile("xorl %%eax,%%eax \n cpuid" ::: "%rax", "%rbx", "%rcx", "%rdx"); //serialize
    uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
