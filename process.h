#pragma once
#include "string.h"
struct pollfd;

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
    void registerPoll();
    /// Remove this from the process-wide event loop
    void unregisterPoll();
    /// Linux poll file descriptor
    virtual pollfd poll() =0;
    /// Callback on new events
    virtual void event(pollfd p) =0;
};

/// Set process CPU scheduling priority (-20 high priority, 19 low priority)
void setPriority(int priority);
/// Execute binary at \a path with command line arguments \a args
void execute(const string& path, const array<string>& args=array<string>());
/// Returns CPU time in milliseconds consumed since start of process
int getCPUTime();

/// Log stack trace skipping top \a skip frames
void logTrace(int skip);

struct Profile {
    string name;
    int time=0;
    Profile(string&& name=""_):name(move(name)){ time=getCPUTime(); }
    ~Profile() { log(name,getCPUTime()-time); }
};
