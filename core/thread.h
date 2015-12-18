#pragma once
/// \file process.h \link Thread threads\endlink, \link Lock synchronization\endlink, process environment and arguments
#include "map.h"
#include "file.h"
#include "function.h"
#include <poll.h>
#include <pthread.h> //pthread
#include <sys/inotify.h>

/*/// Abstract factory pattern (allows construction of class by names)
template<Type I> struct Interface {
    struct AbstractFactory {
        virtual unique<I> constructNewInstance() abstract;
    };
    static map<string, AbstractFactory*>& factories() { static map<string, AbstractFactory*> factories; return factories; }
    template<Type C> struct Factory : AbstractFactory {
        unique<I> constructNewInstance() override { return unique<C>(); }
        Factory(string name) { factories().insert(name, this); }
        static Factory registerFactory;
    };
    static unique<I> instance(const string& name) { return factories().at(name)->constructNewInstance(); }
};
template<Type I> template<Type C> Type Interface<I>::template Factory<C> Interface<I>::Factory<C>::registerFactory;

/// Class to inherit in order to register objects to be instanced depending on first command line argument
struct Application { virtual ~Application() {} };
#define registerApplication(T, name...) Interface<Application>::Factory<T> name ## ApplicationFactoryRegistration (#name)*/

/// Lock is an initially released binary semaphore which can only be released by the acquiring thread
struct Lock : handle<pthread_mutex_t> {
 default_move(Lock);
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

struct Condition : handle<pthread_cond_t> {
 default_move(Condition);
 Condition() { pthread_cond_init(&pointer,0); }
 ~Condition(){ pthread_cond_destroy(&pointer); }
};

/// A semaphore implemented using POSIX mutex, POSIX condition variable, and a counter
struct Semaphore {
 default_move(Semaphore);
 Lock mutex;
 Condition condition;
 int64 counter;
 /// Creates a semaphore with \a count initial ressources
 explicit Semaphore(int64 count=0) : counter(count) {}
 /// Acquires \a count ressources
 void acquire(int64 count) {
  mutex.lock();
  while(counter<count) pthread_cond_wait(&condition, &mutex);
  __sync_sub_and_fetch(&counter, count);
  assert(counter>=0);
  mutex.unlock();
 }
 /// Atomically tries to acquires \a count ressources only if available
 bool tryAcquire(int64 count) {
  mutex.lock();
  if(counter<count) { mutex.unlock(); return false; }
  assert(count>0);
  __sync_sub_and_fetch(&counter, count);
  mutex.unlock();
  return true;
 }
 /// Releases \a count ressources
 void release(int64 count) {
  mutex.lock();
  __sync_add_and_fetch(&counter, count); // Not atomic already ?
  mutex.unlock(); // here ?
  pthread_cond_broadcast(&condition);
  //mutex.unlock(); // or here ?
 }
 /// Returns available ressources \a count
 operator uint64() const { return counter; }
};
inline String str(const Semaphore& o) { return str(o.counter); }

struct Thread;
/// Original thread spawned when this process was forked, terminating this thread leader terminates the whole thread group
extern Thread mainThread;
int __attribute((noreturn)) exit_group(int status);

struct pollfd;
/// Poll is a convenient interface to participate in the event loop
struct Poll : pollfd {
 enum { IDLE=64 };
 Thread& thread; /// Thread monitoring this pollfd

 /// Creates an handle to participate in an event loop, use \a registerPoll when ready
 /// \note May be used without a file descriptor to queue jobs using \a wait, \a event will be called after all system events have been handled
 Poll(int fd=0, int events=POLLIN, Thread& thread=mainThread) : pollfd{fd,(short)events,0}, thread(thread) { if(fd) registerPoll(); }
 no_copy(Poll);
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

typedef unsigned long pthread_t;
/// Concurrently runs an event loop
struct Thread : array<Poll*>, EventFD, Lock, Poll {
 array<Poll*> queue; // Poll objects queued on this thread
 array<Poll*> unregistered; // Poll objects removed while in event loop
 int priority=0; // Thread system priority
 pthread_t thread = 0;
 int tid=0; // Thread system identifier
 Lock runLock;
 bool terminationRequested = false;

 Thread(int priority=0, bool spawn=false);
 ~Thread(){ Poll::fd=0;/*Avoid Thread::unregistered reference in ~Poll*/ }
 explicit operator bool() const { return thread; }

 void setPriority(int priority);
 /// Spawns a thread running an event loop with the given \a priority
 void spawn();
 /// Processes all events on \a polls and tasks on \a queue until terminate is set
 void run();
 /// Processes one queued task
 void event();
 /// Waits on this thread
 void wait();
};
int32 gettid();

struct Job : Poll {
 function<void()> job;
 Job(Thread& thread, function<void()> job, bool queue=true) : Poll(0,0,thread), job(job) { if(queue) this->queue(); }
 void event() override { job(); }
};

/// Flags all threads to terminate as soon as they return to event loop, destroys all global objects and exits process.
void requestTermination(int status=0);

/// Locates an executable
String which(string name);

/// Execute binary at \a path with command line arguments \a args
/// \note if \a wait is false, Returns the PID to be used for wait
int execute(const string path, const ref<string> args={}, bool wait=true, const Folder& workingDirectory=currentWorkingDirectory(), Handle* stdout = 0, Handle* stderr = 0);
/// Waits for any child process to change state
int wait();
/// Waits for process \a pid to change status
/// \note Returns immediatly if process is waitable (already terminated)
int wait(int pid);
/// Returns whether process \a pid is running
bool isRunning(int pid);

struct inotify_event;
/// Watches a folder for new files
struct FileWatcher : File, Poll {
 String path;
 function<void(string)> fileModified;

 FileWatcher(string path, function<void(string)> fileModified)
  : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), path(copyRef(path)), fileModified(fileModified) {
  addWatch(path);
 }
 virtual ~FileWatcher() {}
 void addWatch(string path)  { check(inotify_add_watch(File::fd, strz(path), IN_MODIFY|IN_MOVED_TO), path); }
 void event() override {
  while(poll()) {
   ::buffer<byte> buffer = readUpTo(sizeof(inotify_event) + 256);
   inotify_event e = *(inotify_event*)buffer.data;
   string name = e.len ? string(e.name, e.len-1) : path;
   fileModified(name);
  }
 }
};
