#pragma once
/// \file process.h \link Thread threaded event loops\endlink, \link Semaphore synchronization\endlink, execute, process environment and arguments
#include "array.h"
#include "file.h"

/// A simple semaphore using futex
struct Semaphore {
    int futex;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int count=0):futex(count){}
    /// Acquires \a count ressources
    inline bool acquire(int count) { int val=__sync_sub_and_fetch(&futex, count); if(val<0) { wait(val); return true; } else return false; }
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int count) { int val=__sync_sub_and_fetch(&futex, count); if(val<0) { __sync_fetch_and_add(&futex,count); return false; } else return true; }
    /// Waits for the semaphore
    void wait(int val);
    /// Releases \a count ressources
    inline void release(int count) { int val=__sync_fetch_and_add(&futex, count); if(val<0) wake(); }
    /// Wakes any thread waiting for the semaphore
    void wake();
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

/// Returns original thread spawned when this process was forked, terminating this thread leader terminates the whole thread group
struct Thread& mainThread();

/// Poll is a convenient interface to participate in the event loops
struct Poll : pollfd {
    no_copy(Poll);
    Thread& thread; /// Thread monitoring this pollfd
    /// Allows to queue using \a wait method and \a event callback
    Poll(Thread& thread=mainThread()):thread(thread){}
    void registerPoll();
    /// Registers \a fd to be polled in the event loop
    Poll(int fd, int events=POLLIN, Thread& thread=mainThread()):____(pollfd{fd,(short)events},)thread(thread){}
    /// Removes \a fd from the event loop
    void unregisterPoll();
    ~Poll(){unregisterPoll();}
    /// Schedules an \a event call from \a thread's next poll iteration
    void queue();
    /// Callback on new poll events (or after a \a wait)
    virtual void event() =0;
};

struct EventFD : Stream {
    EventFD();
    void post(){Stream::write(raw<uint64>(1));}
    void read(){Stream::read<uint64>();}
};

/// Concurrently runs an event loop
struct Thread : array<Poll*>, EventFD, Poll {
    int tid=0,priority=0;
    bool terminate=0; // Flag to cleanly terminate a thread
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    Lock lock;
    Map stack;
    /// Spawns a thread running an event loop with the given \a priority
    Thread(int priority=0);
    /// Processes all events on \a polls and tasks on \a queue until #terminate is set
    void run();
    /// Processes all events on \a polls and tasks on \a queue
    bool processEvents();
    /// Processes one queued task
    void event();
};

/// Terminates all auxiliary threads, destroys all file-scope objects and exits process.
void exit();

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
