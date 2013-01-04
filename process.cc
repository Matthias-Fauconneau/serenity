#include "process.h"
#include "linux.h"
#include "file.h"
#include "data.h"
#include "trace.h"

#if NOLIBC
struct rlimit { long cur,max; };
enum {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS};
enum {SIGHUP=1, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM, SIGIO=29};
struct siginfo_t {
    int signo,errno,si_code;
    union {
        struct { long revents; int fd; } poll;
        struct { void *si_addr; };
    };
};
struct ucontext {
    long flags; void* link; struct { void* sp; int flags; long size; } stack;
#if __x86_64__
    long r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,rbx,rdx,rax,rcx,rsp,ip,efl,csgsfs,err,trap,oldmask,cr2;
#elif __arm__
    long trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,pc,cpsr,fault;
#elif __i386__
    long gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,ip,cs,efl,uesp,ss;
#endif
};
enum {EFD_SEMAPHORE=1};
enum {SA_SIGINFO=4, SA_RESTORER=0x4000000, SA_RESTART=0x10000000};
enum {WNOHANG=1};
#else
#include <sys/eventfd.h>
#include <sched.h>
#define signal signal_
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
//FIXME: use pthread
void __attribute((noreturn)) exit_thread(int status) { syscall(SYS_exit, status); __builtin_unreachable(); }
int exit_group(int status) { return syscall(SYS_exit_group, status); }
int tgkill(int tgid, int tid, int sig) { return syscall(SYS_tgkill,tgid,tid,sig); }
int gettid() { return syscall(SYS_gettid); }
#endif
static constexpr ref<byte> fpErrors[] = {""_, "Integer division"_, "Integer overflow"_, "Division by zero"_, "Overflow"_, "Underflow"_, "Precision"_, "Invalid"_, "Denormal"_};

#if __x86_64
// Configures floating-point exceptions
enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
void setExceptions(int except) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r|=0b111111<<7; r &= ~((except&0b111111)<<7); asm volatile("ldmxcsr %0" : : "m" (*&r)); }
#endif

// Log
void log_(const ref<byte>& buffer) { check_(write(2,buffer.data,buffer.size)); }
template<> void log(const ref<byte>& buffer) { log_(string(buffer+"\n"_)); }

// Poll
void Poll::registerPoll() { thread+=this; thread.post(); }
void Poll::unregisterPoll() {Locker lock(thread.lock); if(fd) thread.unregistered<<this;}
void Poll::queue() {Locker lock(thread.lock); thread.queue+= this; thread.post();}

// EventFD
EventFD::EventFD():Stream(eventfd(0,EFD_SEMAPHORE)){}

// Process-wide structures initialized on first usage
// Lock access to thread list
static Lock& threadsLock() { static Lock threadsLock; return threadsLock; }
// Process-wide thread list to trace all threads when one fails and cleanly terminates all threads before exiting
static array<Thread*>& threads() { static array<Thread*> threads; return threads; }
// Handle for the main thread (group leader)
Thread& mainThread() { static Thread mainThread(20); return mainThread; }
// main
void yield() { sched_yield(); }
int main() {
    mainThread().run();
#if THREAD
    while(threads().size()) yield(); // Lets all threads return to event loop
    Locker lock(threadsLock());
    for(Thread* thread: threads()) if(thread!=&mainThread()) tgkill(getpid(),thread->tid,SIGKILL); // Kills any remaining thread
#endif
    return 0; // Destroys all file-scope objects (libc atexit handlers) and terminates using exit_group
}

// Thread
Thread::Thread(int priority):Poll(EventFD::fd,POLLIN,*this) {
    *this<<(Poll*)this; // Adds eventfd semaphore to this thread's monitored pollfds
    Locker lock(threadsLock()); threads()<<this; // Adds this thread to global thread list
    this->priority=priority;
}
#if THREAD
static void* run(void* thread) { ((Thread*)thread)->run(); return 0; }
void Thread::spawn() { pthread_create(&thread,0,&::run,this); }
#endif

void Thread::run() {
    tid=gettid();
    if(priority!=20) check_(setpriority(0,0,priority));
    while(!terminate) processEvents();
    Locker lock(threadsLock());
    threads().removeAll(this);
}
bool Thread::processEvents() {
    uint size=this->size();
    if(size==1) { return terminate=true; }

    pollfd pollfds[size];
    for(uint i: range(size)) pollfds[i]=*at(i);
    if(check__( ::poll(pollfds,size,-1) ) != INTR) {
        for(uint i: range(size)) {
            if(terminate) return true;
            Poll* poll=at(i); int revents=pollfds[i].revents;
            if(revents && !unregistered.contains(poll)) {
                poll->revents = revents;
                poll->event();
            }
        }
    }
    while(unregistered){Locker lock(this->lock); Poll* poll=unregistered.pop(); removeOne(poll); queue.removeOne(poll);}
    return terminate;
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
    Locker lock(threadsLock());
    for(Thread* thread: threads()) {
        thread->terminate=true; // Tries to terminate all other threads cleanly
        if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP); // Logs stack trace of all threads
    }
}

// Signal handler
string trace(int skip, void* ip);
static void handler(int sig, siginfo_t* info, void* ctx) {
#if NOLIBC
    void* ip = (void*)(((ucontext*)ctx)->pc);
#else
#if __x86_64
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP];
#elif __arm
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.arm_pc;
#elif __i386
    void* ip = (void*)((ucontext_t*)ctx)->uc_mcontext.gregs[REG_EIP];
#endif
#endif
    string s = trace(1,ip);
    if(threads().size()>1) log_(string("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
    if(sig!=SIGTRAP) traceAllThreads();
    if(sig==SIGABRT) log("Aborted");
#ifndef __arm
    if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->si_code],")", *(float*)info->si_addr);
#endif
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(info->si_addr));
    if(sig==SIGTERM) log("Terminated");
    exit_thread(0);
}

#if NOLIBC
#if __x86_64
extern void restore_rt() asm ("__restore_rt"); asm(".text; .align 16; __restore_rt:; movq $15, %rax; syscall;");
#elif __arm__
extern void restore_rt() asm ("__restore_rt"); asm(".text; .align 2; .fnstart; .save {r0-r15}; .pad #160; nop; __restore_rt:; mov r7, $119; swi 0; .fnend;");
#else
#error Unsupported architecture
#endif
#endif

void __attribute((constructor(101))) setup_signals() {
    /// Limit stack size to avoid locking system by exhausting memory with recusive calls
    rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit);
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
#if NOLIBC
    struct {
        void (*sigaction) (int, siginfo_t*, ucontext_t*) = &handler;
        long flags = SA_SIGINFO|SA_RESTORER|SA_RESTART;
        void (*restorer)() = &restore_rt;
        uint mask[2] = {0,0};
    } sa;
    check_(sigaction(SIGFPE, &sa, 0, 8));
    check_(sigaction(SIGABRT, &sa, 0, 8));
    check_(sigaction(SIGSEGV, &sa, 0, 8));
    check_(sigaction(SIGTERM, &sa, 0, 8));
    check_(sigaction(SIGTRAP, &sa, 0, 8));
#else
    struct sigaction sa; sa.sa_sigaction=&handler; sa.sa_flags=SA_SIGINFO|SA_RESTART; sa.sa_mask={};
    check_(sigaction(SIGFPE, &sa, 0));
    check_(sigaction(SIGABRT, &sa, 0));
    check_(sigaction(SIGSEGV, &sa, 0));
    check_(sigaction(SIGTERM, &sa, 0));
    check_(sigaction(SIGTRAP, &sa, 0));
#if __x86_64
    //setExceptions(Invalid|Denormal|DivisionByZero|Overflow|Underflow);
#endif
#endif

}

static int recurse=0;
template<> void __attribute((noreturn)) error(const ref<byte>& message) {
    if(recurse==0) {
        recurse++;
        traceAllThreads();
        string s = trace(1,0);
        if(threads().size()>1) log_(string("Thread #"_+dec(gettid())+":\n"_+s)); else log_(s);
        recurse--;
    }
    log(message);
    {Locker lock(threadsLock()); for(Thread* thread: threads()) if(thread->tid==gettid()) { threads().removeAll(thread); break; } }
    exit_thread(0);
}

void exit() {
    Locker lock(threadsLock());
    for(Thread* thread: threads()) { thread->terminate=true; thread->post(); }
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
