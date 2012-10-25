#pragma once
/// \file process.h \link Thread threaded event loops\endlink, \link Semaphore synchronization\endlink, execute, process environment and arguments
#include "array.h"
#include "file.h"

#define PTHREAD 1
#if PTHREAD
#define timespec timespec_
#include <pthread.h>
#undef timespec
#undef CLOCK_REALTIME
#undef CLOCK_THREAD_CPUTIME_ID
/// A semaphore using POSIX mutex, POSIX condition variable, and a counter
struct Semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    long counter;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int count=0):counter(count){pthread_mutex_init(&mutex,0); pthread_cond_init(&condition,0);}
    /// Acquires \a count ressources
    inline void acquire(int count) {pthread_mutex_lock(&mutex); while(counter<count) pthread_cond_wait(&condition,&mutex); counter-=count; pthread_mutex_unlock(&mutex);}
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int count) {pthread_mutex_lock(&mutex); if(counter<count) { pthread_mutex_unlock(&mutex); return false; } counter-=count; pthread_mutex_unlock(&mutex); return true; }
    /// Releases \a count ressources
    inline void release(int count) {pthread_mutex_lock(&mutex); counter+=count; pthread_cond_signal(&condition); pthread_mutex_unlock(&mutex);}
    /// Returns available ressources \a count
    operator int() { return counter; }
};

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
struct Lock {
    pthread_mutex_t mutex;
    Lock(){pthread_mutex_init(&mutex,0);}
    /// Locks the mutex.
    inline void lock() {pthread_mutex_lock(&mutex);}
    /// Atomically lock the mutex only if unlocked.
    inline bool tryLock() { return !pthread_mutex_trylock(&mutex); }
    /// Unlocks the mutex.
    inline void unlock() {pthread_mutex_unlock(&mutex);}
};
#elif 1
#include <mutex>
#include <condition_variable>

/// A semaphore using std::mutex, std::condition_variable, and a counter
struct Semaphore {
    std::mutex mutex;
    std::condition_variable condition;
    long counter;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int count=0):counter(count){}
    /// Acquires \a count ressources
    inline bool acquire(int count) {mutex::scoped_lock lock(mutex); while(counter<count) condition.wait(lock); counter-=count;}
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int count) {mutex::scoped_lock lock(mutex); if(counter<count) return false; counter-=count; return true; }
    /// Releases \a count ressources
    inline void release(int count) {mutex::scoped_lock lock(mutex); counter+=count; condition.notify_one();}
    /// Returns available ressources \a count
    operator int() { return counter; }
};
#else
/// A semaphore using futex
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

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
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
#endif

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
    Poll(Thread& thread=mainThread()):____(pollfd{0,0,0},)thread(thread){}
    void registerPoll();
    /// Registers \a fd to be polled in the event loop
    Poll(int fd, int events=POLLIN, Thread& thread=mainThread()):____(pollfd{fd,(short)events,0},)thread(thread){}
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
#if PTHREAD
    pthread_t thread;
#endif
    int tid=0,priority=0;
    bool terminate=0; // Flag to cleanly terminate a thread
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    Lock lock;
    Map stack;
    Thread(int priority=0);
    /// Spawns a thread running an event loop with the given \a priority
    void spawn();
    /// Processes all events on \a polls and tasks on \a queue until #terminate is set
    void run();
    /// Processes all events on \a polls and tasks on \a queue
    bool processEvents();
    /// Processes one queued task
    void event();
};

/// Flags all threads to terminate as soon as they return to event loop, destroys all file-scope objects and exits process.
void exit();

/// Yields to scheduler
void yield();

/// Execute binary at \a path with command line arguments \a args
void execute(const ref<byte>& path, const ref<string>& args=ref<string>(), bool wait=true);

/// Returns value for environment variable \a name
string getenv(const ref<byte>& name);

/// Returns command line arguments
array< ref<byte> > arguments();

/// Returns standard folders
const Folder& home(); //$HOME
const Folder& config(); //$HOME/.config
const Folder& cache(); //$HOME/.cache
