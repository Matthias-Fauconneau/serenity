#pragma once
#include "string.h"
#include "map.h"
struct pollfd;

extern "C" char* getenv(const char* key);

/// Application can be inherited to interface with the event loop
struct Application {
    /// Flag to exit event loop and quit application
    bool running=true;
    /// Set running flag to false so as to quit the application when returning to the event loop.
    /// \note Use this method for normal termination. \a exit doesn't destruct stack allocated objects.
    void quit() { running=false; }
};

/// Convenience macro to startup an Application with the default event loop
#define Application(App) \
int main(int argc, const char** argv) { \
    array<string> args; for(int i=1;i<argc;i++) args << strz(argv[i]); \
    App* app = new App(move(args)); \
    while(app->running && waitEvents()) {} \
    return 0; \
}

/// Convenience macro to startup an Application with the default event loop only if no Application() is defined
#define Test(App) int main(int argc, const char** argv) __attribute((weak)); Application(App)

/// Poll is an interface for objects needing to participate in event handling
struct Poll {
    /// Add this to the process-wide event loop
    void registerPoll(pollfd);
    /// Remove this to the process-wide event loop
    void unregisterPoll();
    virtual ~Poll() { unregisterPoll(); }
    /// Callback on new events
    virtual void event(pollfd p) =0;
};

/// Wait for and dispatches events to registered Poll objects
/// \return count of registered Poll objects
int waitEvents();

/// Set process CPU scheduling priority (-20 high priority, 19 low priority)
void setPriority(int priority);

/// Return available memory in kB
uint availableMemory();

/// Execute binary at \a path with command line arguments \a args
void execute(const string& path, const array<string>& args=array<string>());

#if DEBUG
/// Returns CPU time in milliseconds consumed since start of process
int getCPUTime();
extern map<const char*, int> profile;
/// Times \a statements and add to the process CPU usage profile
/// \note use getCPUTime to profile kernel time and avoid other interference from other processes (adapted to longer tasks)
#define profile(name, statements ) { int start=getCPUTime(); statements; profile[#name]+=getCPUTime()-start; }
#else
#define profile(name, statements ) { statements; }
#endif

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
