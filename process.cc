#include "process.h"
#include "linux.h"
#include "file.h"
#include "data.h"
#include "trace.h"

#include <sys/eventfd.h>
#include <sched.h>
#define signal signal_
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

void __attribute((noreturn)) exit_thread(int status) { syscall(SYS_exit, status); __builtin_unreachable(); }
int exit_group(int status) { return syscall(SYS_exit_group, status); }
int tgkill(int tgid, int tid, int sig) { return syscall(SYS_tgkill,tgid,tid,sig); }
int gettid() { return syscall(SYS_gettid); }

static constexpr ref<byte> fpErrors[] = {""_, "Integer division"_, "Integer overflow"_, "Division by zero"_, "Overflow"_, "Underflow"_, "Precision"_, "Invalid"_, "Denormal"_};

#if __x86_64
// Configures floating-point exceptions
enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
void setExceptions(int except) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r|=0b111111<<7; r &= ~((except&0b111111)<<7); asm volatile("ldmxcsr %0" : : "m" (*&r)); }
#endif

// Lock access to thread list
static Lock threadsLock __attribute((init_priority(1001)));
// Process-wide thread list to trace all threads when one fails and cleanly terminates all threads before exiting
static array<Thread*> threads __attribute((init_priority(1002)));
// Handle for the main thread (group leader)
Thread mainThread __attribute((init_priority(1003))) __(20);

// Log
void log_(const ref<byte>& buffer) { check_(write(2,buffer.data,buffer.size)); }
template<> void log(const ref<byte>& buffer) { log_(string(buffer+"\n"_)); }

// Poll
void Poll::registerPoll() {
    Locker lock(thread.lock);
    if(thread.unregistered.contains(this)) { thread.unregistered.removeAll(this); }
    else { assert(!thread.contains(this)); thread<<this; }
    thread.post();
}
void Poll::unregisterPoll() {Locker lock(thread.lock); if(fd) thread.unregistered<<this;}
void Poll::queue() {Locker lock(thread.lock); thread.queue.appendOnce(this); thread.post();}

// EventFD
EventFD::EventFD():Stream(eventfd(0,EFD_SEMAPHORE)){}

// main
int main() {
    mainThread.run();
    exit(); // Signals termination to all threads
    for(Thread* thread: threads) { void* status; pthread_join(thread->thread,&status); } // Waits for all threads to terminate
    return 0; // Destroys all file-scope objects (libc atexit handlers) and terminates using exit_group
}

// Thread
Thread::Thread(int priority):Poll(EventFD::fd,POLLIN,*this) {
    *this<<(Poll*)this; // Adds eventfd semaphore to this thread's monitored pollfds
    Locker lock(threadsLock); threads<<this; // Adds this thread to global thread list
    this->priority=priority;
}
static void* run(void* thread) { ((Thread*)thread)->run(); return 0; }
void Thread::spawn() { assert(!thread); pthread_create(&thread,0,&::run,this); }

void Thread::run() {
    tid=gettid();
    if(priority!=20) check_(setpriority(0,0,priority));
    while(!terminate) {
        uint size=this->size();
        if(size==1) break; // Terminates when no Poll objects are registered

        pollfd pollfds[size];
        for(uint i: range(size)) pollfds[i]=*at(i); //Copy pollfds as objects might unregister while processing in the loop
        if(check__( ::poll(pollfds,size,-1) ) != INTR) {
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

void traceAllThreads() {
    Locker lock(threadsLock);
    for(Thread* thread: threads) {
        thread->terminate=true; // Tries to terminate all other threads cleanly
        if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP); // Logs stack trace of all threads
    }
}

// Signal handler
string trace(int skip, void* ip);
static void handler(int sig, siginfo_t* info, void* ctx) {
#if __x86_64
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP];
#elif __arm
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.arm_pc;
#elif __i386
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_EIP];
#endif
    string s = trace(1,ip);
    if(threads.size()>1) log_(string("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
    if(sig!=SIGTRAP) traceAllThreads();
    if(sig==SIGABRT) log("Aborted");
#ifndef __arm
    if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->si_code],")", *(float*)info->si_addr);
#endif
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(info->si_addr));
    if(sig==SIGTERM) log("Terminated");
    exit_thread(0);
}

void __attribute((constructor(101))) setup_signals() {
    /// Limit stack size to avoid locking system by exhausting memory with recusive calls
    rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit);
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct sigaction sa; sa.sa_sigaction=&handler; sa.sa_flags=SA_SIGINFO|SA_RESTART; sa.sa_mask={};
    check_(sigaction(SIGFPE, &sa, 0));
    check_(sigaction(SIGABRT, &sa, 0));
    check_(sigaction(SIGSEGV, &sa, 0));
    check_(sigaction(SIGTERM, &sa, 0));
    check_(sigaction(SIGTRAP, &sa, 0));
}

static int recurse=0;
template<> void __attribute((noreturn)) error(const ref<byte>& message) {
    if(recurse==0) {
        recurse++;
        traceAllThreads();
        string s = trace(1,0);
        if(threads.size()>1) log_(string("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
        recurse--;
    }
    log(message);
    {Locker lock(threadsLock); for(Thread* thread: threads) if(thread->tid==gettid()) { threads.removeAll(thread); break; } }
    exit_thread(0);
}

void exit() {
    Locker lock(threadsLock);
    for(Thread* thread: threads) { thread->terminate=true; thread->post(); }
}

// Environment
void execute(const ref<byte>& path, const ref<string>& args, bool wait) {
    if(!existsFile(path)) { warn("Executable not found",path); return; }

    array<stringz> args0(1+args.size);
    args0 << strz(path);
    for(const auto& arg: args) args0 << strz(arg);
    const char* argv[args0.size()+1];
    for(uint i: range(args0.size())) argv[i]=args0[i];
    argv[args0.size()]=0;

    array< ref<byte> > env0;
    static string environ = File("proc/self/environ"_).readUpTo(4096);
    for(TextData s(environ);s;) env0<<s.until('\0');

    const char* envp[env0.size()+1];
    for(uint i: range(env0.size())) envp[i]=env0[i].data;
    envp[env0.size()]=0;

    int pid = fork();
    if(pid==0) { if(!execve(strz(path),(char*const*)argv,(char*const*)envp)) exit_group(-1); }
    else if(wait) wait4(pid,0,0,0);
    else wait4(pid,0,WNOHANG,0);
}

string getenv(const ref<byte>& name) {
    for(TextData s = File("proc/self/environ"_).readUpTo(4096);s;) {
        ref<byte> key=s.until('='); ref<byte> value=s.until('\0');
        if(key==name) return string(value);
    }
    warn("Undefined environment variable"_, name);
    return string();
}

array< ref<byte> > arguments() {
    static string arguments = File("proc/self/cmdline"_).readUpTo(4096);
    return split(section(arguments,0,1,-1),0);
}

const Folder& home() { static Folder home(getenv("HOME"_)); return home; }
const Folder& config() { static Folder config=Folder(".config"_,home(),true); return config; }
const Folder& cache() { static Folder cache=Folder(".cache"_,home(),true); return cache; }

string userErrors;
