#include "thread.h"
#include "string.h"
#include "linux.h"
#include "data.h"
#include "trace.h"
#include <pthread.h> //pthread
#include <sys/eventfd.h>
#include <sched.h>
#define signal signal_
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <pwd.h>

// Memory
uint64 traceMemoryAllocation = -1;

// Log
void log_(const string& buffer) { check_(write(2,buffer.data,buffer.size)); }
template<> void log(const string& buffer) { log_(buffer+"\n"_); }

// Poll
void Poll::registerPoll() {
    Locker lock(thread.lock);
    if(thread.unregistered.contains(this)) { thread.unregistered.removeAll(this); }
    else if(!thread.contains(this)) thread<<this;
    thread.post(); // Reset poll to include this new descriptor (FIXME: only if not current)
}
void Poll::unregisterPoll() {Locker lock(thread.lock); if(fd) thread.unregistered<<this;}
void Poll::queue() {Locker lock(thread.lock); thread.queue+=this; thread.post();}

// Threads

// Lock access to thread list
static Lock threadsLock __attribute((init_priority(103)));
// Process-wide thread list to trace all threads when one fails and cleanly terminates all threads before exiting
static array<Thread*> threads __attribute((init_priority(104)));
// Handle for the main thread (group leader)
Thread mainThread __attribute((init_priority(105))) (20);

void __attribute((noreturn)) exit_thread(int status) { syscall(SYS_exit, status); __builtin_unreachable(); }
int __attribute((noreturn)) exit_group(int status) { syscall(SYS_exit_group, status); __builtin_unreachable(); }
int tgkill(int tgid, int tid, int sig) { return syscall(SYS_tgkill,tgid,tid,sig); }
int gettid() { return syscall(SYS_gettid); }

EventFD::EventFD():Stream(eventfd(0,EFD_SEMAPHORE)){}

Thread::Thread(int priority):Poll(EventFD::fd,POLLIN,*this) {
    *this<<(Poll*)this; // Adds eventfd semaphore to this thread's monitored pollfds
    Locker lock(threadsLock); threads<<this; // Adds this thread to global thread list
    this->priority=priority;
}
void Thread::setPriority(int priority) { setpriority(0,0,priority); }
static void* run(void* thread) { ((Thread*)thread)->run(); return 0; }
void Thread::spawn() { assert(!thread); pthread_create(&thread,0,&::run,this); }

void Thread::run() {
    tid=gettid();
    if(priority) setpriority(0,0,priority); // No check_ as this is not critical
    assert_(this->size>1 || queue || (this==&mainThread && threads.size>1), "Thread spawned with no associated poll descriptors");
    while(!terminate) {
        uint size=this->size;
        if(size==1 && !queue && !(this==&mainThread && threads.size>1)) break; // Terminates when no Poll objects are registered (except main)

        pollfd pollfds[size];
        for(uint i: range(size)) pollfds[i]=*at(i); //Copy pollfds as objects might unregister while processing in the loop
        if(check( ::poll(pollfds,size,-1) ) != INTR) {
            for(uint i: range(size)) {
                if(terminate) break;
                Poll* poll=at(i); int revents=pollfds[i].revents;
                if(revents && !unregistered.contains(poll)) {
                    poll->revents = revents;
                    poll->event();
                }
            }
        }
        while(unregistered){Locker lock(this->lock); Poll* poll=unregistered.pop(); removeAll(poll); queue.removeAll(poll);}
    }
    Locker lock(threadsLock); threads.removeAll(this);
    thread = 0;
}

void Thread::event() {
    EventFD::read();
    if(queue){
        Poll* poll;
        {Locker lock(this->lock);
            poll=queue.take(0);
            if(unregistered.contains(poll)) return;
        }
        poll->revents=IDLE;
        poll->event();
    }
}

// Debugger
String trace(int skip, void* ip);
void traceAllThreads() {
    Locker lock(threadsLock);
    for(Thread* thread: threads) {
        thread->terminate=true; // Tries to terminate all other threads cleanly
        if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP); // Logs stack trace of all threads
    }
}
static constexpr string fpErrors[] = {""_, "Integer division"_, "Integer overflow"_, "Division by zero"_, "Overflow"_,
                                      "Underflow"_, "Precision"_, "Invalid"_, "Denormal"_};
static void handler(int sig, siginfo_t* info, void* ctx) {
#if __x86_64
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP];
#elif __arm
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.arm_pc;
#elif __i386
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_EIP];
#endif
    if(sig==SIGSEGV) log("Segmentation fault"_);
    String s = trace(1,ip);
    if(threads.size>1) log_(String("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
    if(sig!=SIGTRAP) traceAllThreads();
    if(sig==SIGABRT) log("Aborted");
#ifndef __arm
    if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->si_code],")", *(float*)info->si_addr);
#endif
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(info->si_addr));
    if(sig==SIGTERM) log("Terminated");
    pthread_exit((void*)-1);
    exit_thread(-1);
}
#if __x86_64
// Configures floating-point exceptions
void setExceptions(uint except) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r|=0b111111<<7; r &= ~((except&0b111111)<<7); asm volatile("ldmxcsr %0" : : "m" (*&r)); }
#endif
void __attribute((constructor(102))) setup_signals() {
    /// Limit stack size to avoid locking system by exhausting memory with recursive calls
    //rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit);
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct sigaction sa; sa.sa_sigaction=&handler; sa.sa_flags=SA_SIGINFO|SA_RESTART; sa.sa_mask={0};
    check_(sigaction(SIGABRT, &sa, 0));
    check_(sigaction(SIGSEGV, &sa, 0));
    check_(sigaction(SIGTERM, &sa, 0));
    check_(sigaction(SIGTRAP, &sa, 0));
    check_(sigaction(SIGFPE, &sa, 0));
#if __x86_64
    setExceptions(Invalid | Denormal | DivisionByZero | Overflow | Underflow);
#endif
}

template<> void warn(const string& message) {
    static bool reentrant = false;
    if(!reentrant) { // Avoid hangs if tracing errors
        reentrant = true;
        String s = trace(1,0);
        log_(s);
        reentrant = false;
    }
    log(message);
}

template<> void __attribute((noreturn)) error(const string& message) {
    log(message); // In case, tracing crashes
    static bool reentrant = false;
    if(!reentrant) { // Avoid hangs if tracing errors
        reentrant = true;
        traceAllThreads();
        String s = trace(1,0);
        if(threads.size>1) log_(String("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
        reentrant = false;
    }
    log(message);
    exit(-1); // Signals all threads to terminate
    {Locker lock(threadsLock); for(Thread* thread: threads) if(thread->tid==gettid()) { threads.removeAll(thread); break; } } // Removes this thread from list
    __builtin_trap(); //TODO: detect if running under debugger
    exit_thread(-1); // Exits this thread
}

static int exitStatus;
// Entry point
int main() {
    if(mainThread.size>1 || mainThread.queue || threads.size>1) mainThread.run();
    exit(0); // Signals all threads to terminate
    for(Thread* thread: threads) if(thread->thread) { void* status; pthread_join(thread->thread,&status); } // Waits for all threads to terminate
    return exitStatus; // Destroys all file-scope objects (libc atexit handlers) and terminates using exit_group
}

void exit(int status) {
    Locker lock(threadsLock);
    for(Thread* thread: threads) { thread->terminate=true; thread->post(); }
    exitStatus = status;
}

// Environment
int execute(const string& path, const ref<string>& args, bool wait, const Folder& workingDirectory) {
    if(!existsFile(path)) { warn("Executable not found",path); return -1; }

    array<String> args0(1+args.size);
    args0 << strz(path);
    for(const auto& arg: args) args0 << strz(arg);
    const char* argv[args0.size+1];
    for(uint i: range(args0.size)) argv[i] = args0[i].data;
    argv[args0.size]=0;

    array<string> env0;
    static String environ = File("proc/self/environ"_).readUpTo(4096);
    for(TextData s(environ);s;) env0 << s.until('\0');

    const char* envp[env0.size+1];
    for(uint i: range(env0.size)) envp[i]=env0[i].data;
    envp[env0.size]=0;

    int cwd = workingDirectory.fd;
    int pid = fork();
    if(pid==0) {
        if(cwd!=AT_FDCWD) check_(fchdir(cwd));
        if(!execve(strz(path).data, (char*const*)argv, (char*const*)envp)) exit_group(-1);
        __builtin_unreachable();
    }
    else if(wait) return ::wait(pid);
    else { wait4(pid,0,WNOHANG,0); return pid; }
}
int wait() { return wait4(-1,0,0,0); }
int64 wait(int pid) { void* status=0; wait4(pid,&status,0,0); return (int64)status; }

string getenv(const string& name, string value) {
    static String environ = File("proc/self/environ"_).readUpTo(8192);
    for(TextData s(environ);s;) {
        string key=s.until('='); string value=s.until('\0');
        if(key==name) return value;
    }
    if(!value) warn("Undefined environment variable"_, name);
    return value;
}

array<string> arguments() {
    static String cmdline = File("proc/self/cmdline"_).readUpTo(4096);
    assert(cmdline.size<4096);
    return split(section(cmdline,0,1,-1),0);
}

string homePath() { return getenv("HOME"_,str((const char*)getpwuid(geteuid())->pw_dir)); }
const Folder& home() { static Folder home(homePath()); return home; }
const Folder& config() { static Folder config(".config"_,home(),true); return config; }
const Folder& cache() { static Folder cache(".cache"_,home(),true); return cache; }
