#pragma once
#include "array.h"

struct pollfd { int fd; short events, revents; };
enum {POLLIN = 1, POLLOUT=4, POLLERR=8, POLLHUP = 16, POLLNVAL=32, IDLE=64};

/// Poll is a convenient interface to participate in the process-wide event loop
struct Poll : pollfd {
    no_copy(Poll) Poll(){fd=events=revents=0;}
    virtual ~Poll() { unregisterPoll(); }
    /// Registers this file descriptor to be polled in the process-wide event loop
    /// \note Objects should not move while registered (i.e allocated directly on heap and not as a an array value)
    void registerPoll(int fd, short events=POLLIN);
    void registerPoll(short events=POLLIN);
    /// Removes this file descriptor from the process-wide event poll loop
    void unregisterPoll();
    /// Schedules an \a event call after all process-wide outstanding poll events have beem processed
    void wait();
    /// Callback on new poll events (or after a \a wait)
    virtual void event() =0;
};

/// Dispatches events to registered Poll objects
/// \return count of registered Poll objects
int dispatchEvents();

/// Application can be inherited to interface with the event loop
struct Application {
    /// Setup signal handlers
    Application();
    /// Flag to exit event loop and quit application
    bool running=true;
    /// Set running flag to false so as to quit the application when returning to the event loop.
    /// \note Use this method for normal termination. \a exit doesn't destruct stack allocated objects.
    void quit() { running=false; }
};

/// Macro to compile an executable entry point starting an Application with the default event loop
#define Application(App) int main() { for(App app;app.running && dispatchEvents();); return 0; }

/// Execute binary at \a path with command line arguments \a args
void execute(const ref<byte>& path, const ref<string>& args=ref<string>(), bool wait=true);

/// Set process CPU scheduling priority (-20 high priority, 19 low priority)
void setPriority(int priority);

#if __x86_64__
inline uint64 rdtsc() {
    uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
/// Returns the number of cycles used to execute \a statements
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
#endif

ref<byte> getenv(const ref<byte>& name);
