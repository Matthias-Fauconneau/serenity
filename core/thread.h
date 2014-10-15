#pragma once
/// \file process.h \link Thread threads\endlink, \link Lock synchronization\endlink, process environment and arguments
#include "map.h"
#include "data.h"
#include <typeinfo>
#include "file.h"
#include <poll.h>
#include <pthread.h> //pthread

/// Abstract factory pattern (allows construction of class by names)
template<class I> struct Interface {
    struct AbstractFactory {
        /// Returns the version of this implementation
        virtual string version() abstract;
        virtual unique<I> constructNewInstance() abstract;
    };
    static map<string, AbstractFactory*>& factories() { static map<string, AbstractFactory*> factories; return factories; }
    template<class C> struct Factory : AbstractFactory {
        string version() override { return __DATE__ " " __TIME__ ""_; }
        unique<I> constructNewInstance() override { return unique<C>(); }
        Factory(string name) { factories().insert(name, this); }
        Factory() : Factory(({ TextData s (str(typeid(C).name())); s.integer(); s.identifier(); })) {}
        static Factory registerFactory;
    };
    static string version(const string& name) { return factories().at(name)->version(); }
    static unique<I> instance(const string& name) { return factories().at(name)->constructNewInstance(); }
};
template<class I> template<class C> typename Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory;

/// Class to inherit in order to register objects to be instanced depending on first command line argument
struct Application { virtual ~Application() {} };
#define registerApplication(T, name...) Interface<Application>::Factory<T> name ## ApplicationFactoryRegistration (#name)

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
struct Lock : handle<pthread_mutex_t> {
    Lock() { pthread_mutex_init(&pointer,0); }
    ~Lock() { pthread_mutex_destroy(&pointer); }
    /// Locks the mutex.
    void lock() { pthread_mutex_lock(&pointer); }
    /// Atomically lock the mutex only if unlocked.
    bool tryLock() { return !pthread_mutex_trylock(&pointer); }
    /// Unlocks the mutex.
    void unlock() { pthread_mutex_unlock(&pointer); }
};

/// Convenience class to automatically unlock a mutex
struct Locker {
    Lock& lock;
    Locker(Lock& lock):lock(lock){lock.lock();}
    ~Locker(){lock.unlock();}
};

/// Original thread spawned when this process was forked, terminating this thread leader terminates the whole thread group
extern struct Thread mainThread;

/// Poll is a convenient interface to participate in the event loop
struct Poll : pollfd {
    enum { IDLE=64 };
    Poll(const Poll&)=delete; Poll& operator=(const Poll&)=delete;
    Thread& thread; /// Thread monitoring this pollfd
    /// Creates an handle to participate in an event loop, use \a registerPoll when ready
    /// \note May be used without a file descriptor to queue jobs using \a wait, \a event will be called after all system events have been handled
    Poll(int fd=0, int events=POLLIN, Thread& thread=mainThread) : pollfd{fd,(short)events,0}, thread(thread) { if(fd) registerPoll(); }
    ~Poll(){ if(fd) unregisterPoll(); }
    /// Registers \a fd to the event loop
    void registerPoll();
    /// Unregisters \a fd from the event loop
    void unregisterPoll();
    /// Schedules an \a event call from \a thread's next poll iteration
    void queue();
    /// Callback on new poll events (or when thread is idle when triggered by \a queue)
    virtual void event() abstract;
};

/// Pollable semaphore
struct EventFD : Stream {
    EventFD();
    void post(){Stream::write(raw<uint64>(1));}
    void read(){Stream::read<uint64>();}
};

/// Concurrently runs an event loop
struct Thread : array<Poll*>, EventFD, Poll {
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    int priority=0; // Thread system priority
    int tid=0; // Thread system identifier
    pthread_t thread;
    Lock lock;

    Thread(int priority=0);
    ~Thread(){Poll::fd=0;/*Avoid Thread::unregistered reference in ~Poll*/}
    void setPriority(int priority);
    /// Spawns a thread running an event loop with the given \a priority
    void spawn();
    /// Processes all events on \a polls and tasks on \a queue until terminate is set
    void run();
    /// Processes one queued task
    void event();
};

/// Flags all threads to terminate as soon as they return to event loop, destroys all global objects and exits process.
void exit(int status=0);

enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
void setExceptions(uint except);

/// Returns command line arguments
ref<string> arguments();

/// Returns value for environment variable \a name
string getenv(const string name, string value="");

/// Returns standard folders
const Folder& home(); //$HOME ?: pwuid->pw_dir

/// Locates an executable
String which(string name);

/// Execute binary at \a path with command line arguments \a args
/// \note if \a wait is false, Returns the PID to be used for wait
int execute(const string path, const ref<string> args={}, bool wait=true, const Folder& workingDirectory=currentWorkingDirectory());
/// Waits for any child process to change state
int wait();
/// Waits for process \a pid to change state
/// \note Returns immediatly if process is waitable (already terminated)
int64 wait(int pid);

