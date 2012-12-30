#pragma once
/// \file process.h \link Thread threaded event loops\endlink, \link Semaphore synchronization\endlink, execute, process environment and arguments
#include "array.h"
#include "file.h"
#include "function.h"

#define THREAD 1
#if THREAD
extern "C" {
typedef unsigned long int pthread_t;
struct pthread_mutex { int lock; uint count; int owner; uint nusers; int kind, spins; void* prev,*next; };
struct pthread_cond { int lock; uint futex; uint64 total_seq, wakeup_seq, woken_seq; void* mutex; uint nwaiters; uint broadcast_seq; };
int pthread_create(pthread_t* thread, void* attr, void *(*start_routine)(void*), void* arg);
int pthread_join(pthread_t thread, void **status);
int pthread_mutex_init(pthread_mutex* mutex, const void* attr);
int pthread_mutex_trylock(pthread_mutex* mutex);
int pthread_mutex_lock(pthread_mutex* mutex);
int pthread_mutex_unlock(pthread_mutex* mutex);
int pthread_cond_init(pthread_cond* cond, const void* attr);
int pthread_cond_wait(pthread_cond* cond, pthread_mutex* mutex);
int pthread_cond_signal(pthread_cond* cond);
}
#define pthread(statements...) statements
#else
#define pthread(statements...)
#endif

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
struct Lock {
    pthread( pthread_mutex mutex; )
    Lock(){ pthread( pthread_mutex_init(&mutex,0); ) }
    /// Locks the mutex.
    inline void lock() { pthread( pthread_mutex_lock(&mutex); ) }
    /// Atomically lock the mutex only if unlocked.
    pthread( inline bool tryLock() { return !pthread_mutex_trylock(&mutex); } )
    /// Unlocks the mutex.
    inline void unlock() { pthread( pthread_mutex_unlock(&mutex); ) }
};

/// Convenience class to automatically unlock a mutex
struct Locker {
    Lock& lock;
    Locker(Lock& lock):lock(lock){lock.lock();}
    ~Locker(){lock.unlock();}
};

/// A semaphore using POSIX mutex, POSIX condition variable, and a counter
struct Semaphore {
    Lock mutex;
    pthread( pthread_cond condition; )
    long counter;
    /// Creates a semaphore with \a count initial ressources
    Semaphore(int count=0) : counter(count) { pthread( pthread_cond_init(&condition,0); ) }
    /// Acquires \a count ressources
    inline void acquire(int count) {
        pthread( while(counter<count) pthread_cond_wait(&condition,&mutex.mutex); )
        counter-=count; assert(counter>=0);
    }
    /// Atomically tries to acquires \a count ressources only if available
    inline bool tryAcquire(int count) {	
        if(counter<count) return false; counter-=count; return true;
    }
    /// Releases \a count ressources
    inline void release(int count) {
        Locker lock(mutex);
        counter+=count; 
        pthread( pthread_cond_signal(&condition); )
    }
    /// Returns available ressources \a count
    operator int() { return counter; }
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
    bool terminate=0; // Flag to cleanly terminate a thread
    array<Poll*> queue; // Poll objects queued on this thread
    array<Poll*> unregistered; // Poll objects removed while in event loop
    int priority=0; // Thread system priority
    int tid=0; // Thread system identifier
    pthread( pthread_t thread; )
    Lock lock;
    Map stack;

    Thread(int priority=0);
    /// Spawns a thread running an event loop with the given \a priority
    pthread( void spawn(); )
    /// Processes all events on \a polls and tasks on \a queue until #terminate is set
    void run();
    /// Processes all events on \a polls and tasks on \a queue
    bool processEvents();
    /// Processes one queued task
    void event();
};

#if THREAD
/// Runs a loop in parallel
template<int N=8> struct parallel {
    uint iterationCount;
    function<void(uint)> delegate;
    uint counter=0;
    static void* start_routine(parallel* this_) {
        for(;;) { uint i=__sync_fetch_and_add(&this_->counter,1);
            if(i>=this_->iterationCount) break;
            this_->delegate(i);
        }
        return 0;
    }
    template<class F> parallel(uint iterationCount, F f) : iterationCount(iterationCount), delegate(f) {
        pthread_t threads[N-1];
        for(int i=0;i<N-1;i++) pthread_create(&threads[i],0,(void*(*)(void*))start_routine,this);
        start_routine(this);
        for(int i=0;i<N-1;i++) { void* status; pthread_join(threads[i],&status); }
    }
};
#endif

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

// Aggregates non-fatal errors shown to user
extern string userErrors;
template<class... Args> void userError(const Args&... args) { if(!userErrors) userErrors << str(args ___)+"\n"_; }
