#pragma once
#include "string.h"

/// Poll is an interface for objects needing to participate in event handling
struct Poll {
    no_copy(Poll)
    Poll(){}
    /// Add this to the process-wide event loop
    /// \note Objects should not move while registered (i.e allocated directly on heap and not as a an array value)
    void registerPoll(const struct pollfd&);
    /// Remove this from the process-wide event loop
    void unregisterPoll();
    /// Remove an fd from the process-wide event loop
    static void unregisterPoll(int fd);
    /// Wait for all outstanding poll events to be processed before calling \a event again
    void wait();
    virtual ~Poll() { unregisterPoll(); }
    /// Callback on new events
    virtual void event(const struct pollfd&) =0;
};

/// Dispatches events to registered Poll objects
/// \return count of registered Poll objects
int dispatchEvents();

/// Application can be inherited to interface with the event loop
struct Application {
    /// Flag to exit event loop and quit application
    bool running=true;
    /// Set running flag to false so as to quit the application when returning to the event loop.
    /// \note Use this method for normal termination. \a exit doesn't destruct stack allocated objects.
    void quit() { running=false; }
};

/// Macro to compile an executable entry point starting an Application with the default event loop
#define Application(App) int main() { void init(); init(); for(App app;app.running && dispatchEvents();); return 0; }

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

ref<byte> getenv(const ref<byte>& name);
