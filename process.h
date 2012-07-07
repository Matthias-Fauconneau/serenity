#pragma once
#include "string.h"

struct pollfd { int fd; short events, revents; };
enum { POLLIN = 1, POLLOUT=4, POLLHUP = 16 };

/// Application can be inherited to interface with the event loop
struct Application {
    /// Flag to exit event loop and quit application
    bool running=true;
    /// Set running flag to false so as to quit the application when returning to the event loop.
    /// \note Use this method for normal termination. \a exit doesn't destruct stack allocated objects.
    void quit() { running=false; }
};

/// Convenience macro to startup an Application with the default event loop
void setupHeap();
void catchErrors();
void openDisplay();
#define Application(App) \
extern "C" void _start(int, int, int, int, int argc, const char* arg0, int, int, int, int, int, int, int, int, int, int, int) { \
    setupHeap(); catchErrors(); openDisplay(); \
    array<string> args; for(int i=1;i<argc;i++) args << str(*(&arg0-i)); \
    App app(move(args)); \
    while(app.running && dispatchEvents(true)) {} \
    exit(0); \
}

/// Poll is an interface for objects needing to participate in event handling
struct Poll {
    /// Add this to the process-wide event loop
    void registerPoll(pollfd);
    /// Remove this to the process-wide event loop
    void unregisterPoll();
    /// Wait for all outstanding poll events to be processed before calling \a event again
    void wait();
    virtual ~Poll() { unregisterPoll(); }
    /// Callback on new events
    virtual void event(pollfd) =0;
};

/// Dispatches events to registered Poll objects
/// \return count of registered Poll objects
int dispatchEvents(bool wait);

/// Execute binary at \a path with command line arguments \a args
void execute(const string& path, const array<string>& args=array<string>());

/// Set process CPU scheduling priority (-20 high priority, 19 low priority)
void setPriority(int priority);

#if __x86_64__
inline uint64 rdtsc() {
    uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
/// Returns the number of cycles used to execute \a statements
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
#endif

#if PROCFS
/// Return available memory in kB
uint availableMemory();
#endif

/// Log the corresponding assembly the first time \a statements is executed
void disasm(array<ubyte> code);
#define disasm( statements ) { \
    begin: statements; end: \
    static bool once=false; if(!once) disasm(array<ubyte>((ubyte*)&&begin,(ubyte*)&&end)), once=true; \
}
