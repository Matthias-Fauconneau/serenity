#pragma once
#include "array.h"
#include "file.h"
#include "debug.h"
#include "linux.h"

/// A simple semaphore using futex
struct Semaphore {
    int futex;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int count=0):futex(count){}
    /// Acquires \a count ressources
    inline void acquire(int count) { int val=__sync_sub_and_fetch(&futex, count); if(unlikely(val<0)) wait(futex,val); }
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int count) { int val=__sync_sub_and_fetch(&futex, count); if(unlikely(val<0)) { __sync_fetch_and_add(&futex,count); return false; } else return true; }
    /// Waits for the semaphore
    static void wait(int& futex, int val);
    /// Releases \a count ressources
    inline void release(int count) { int val=__sync_fetch_and_add(&futex, count); if(val<0) wake(futex); }
    /// Wakes any thread waiting for the semaphore
    static void wake(int& futex);
    /// Returns available ressources \a count
    operator int() { return futex; }
};

/// Lock is an initially released binary semaphore
struct Lock : Semaphore {
    int owner=0;//keep same layout between debug/release
    debug(void checkRecursion(); void setOwner(); void checkOwner();)
    Lock():Semaphore(1){}
    /// Locks the mutex.
    inline void lock() { debug(checkRecursion();) acquire(1); debug(setOwner();) }
    /// Atomically lock the mutex only if unlocked.
    inline bool tryLock() { debug(checkRecursion();) if(tryAcquire(1)) { debug(setOwner();) return true; } else return false; }
    /// Unlocks the mutex.
    inline void unlock() { debug(checkOwner();) release(1); }
};

/// Convenience class to automatically unlock a mutex
struct Locker {
    Lock& lock;
    Locker(Lock& lock):lock(lock){lock.lock();}
    ~Locker(){lock.unlock();}
};

/// Original thread spawned when this process was forked, terminating this thread leader terminates the whole thread group
extern struct Thread defaultThread;

/// Poll is a convenient interface to participate in the event loops
struct Poll : pollfd {
    no_copy(Poll)
    Thread& thread;
    /// Allows to queue using \a wait method and \a event callback
    Poll(Thread& thread=defaultThread):thread(thread){}
    /// Registers \a fd to be polled in the event loop
    Poll(int fd, int events=POLLIN, Thread& thread=defaultThread);
    /// Removes \a fd from the event loop
    ~Poll();
    /// Schedules an \a event call from \a thread's next poll iteration
    void queue();
    /// Callback on new poll events (or after a \a wait)
    virtual void event() =0;
};

struct EventFD : Stream {
    EventFD();
    void post(){write(raw<uint64>(1));}
    void read(){Stream::read<uint64>();}
};

struct Thread : array<Poll*>, EventFD, Poll {
    int tid=0,priority=0;
    bool terminate=0; // Flag to cleanly terminate a thread
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    Lock lock;
    Thread();
    /// Spawns a thread executing \a run
    void spawn(int priority=0);
    /// Processes all events on \a polls and tasks on \a queue
    int run();
    /// Processes one queued task
    void event();
};

/// Application provides a \a quit slot to cleanly terminate the default thread
struct Application { void quit() { defaultThread.terminate=2; } };
/// Macro to compile an executable entry point running an Application
#define Application(Application) int main() {extern void init(); init(); {Application app; defaultThread.run();} extern void exit(); exit();}

/// Execute binary at \a path with command line arguments \a args
void execute(const ref<byte>& path, const ref<string>& args=ref<string>(), bool wait=true);

/// Returns value for environment variable \a name
ref<byte> getenv(const ref<byte>& name);

/// Returns command line arguments
array< ref<byte> > arguments();

/// Returns standard folders
const Folder& home(); //$HOME
const Folder& config(); //$HOME/.config
const Folder& cache(); //$HOME/.cache
