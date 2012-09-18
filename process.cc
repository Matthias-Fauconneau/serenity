#include "process.h"
#include "linux.h"
#include "file.h"
#include "data.h"
#include "debug.h"

/// Linux
enum{FUTEX_WAIT,FUTEX_WAKE};
enum{CLONE_VM=0x00000100,CLONE_FS=0x00000200,CLONE_FILES=0x00000400,CLONE_SIGHAND=0x00000800,CLONE_THREAD=0x00010000,CLONE_IO=0x80000000};
struct rlimit { long cur,max; };
enum {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS};
enum {SIGHUP=1, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM, SIGIO=29};
struct siginfo {
    int signo,errno,code;
    union {
        struct { long revents; int fd; } poll;
        struct { void *addr; } fault;
    };
};
struct ucontext {
    long flags; ucontext *link; struct { void* sp; int flags; long size; } stack;
#if __x86_64__
    long r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,rbx,rdx,rax,rcx,rsp,ip,efl,csgsfs,err,trap,oldmask,cr2;
#elif __arm__
    long trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,ip,cpsr,fault;
#elif __i386__
    long gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,ip,cs,efl,uesp,ss;
#endif
};
static constexpr ref<byte> fpErrors[] = {""_, "Integer division"_, "Integer overflow"_, "Division by zero"_, "Overflow"_, "Underflow"_, "Precision"_, "Invalid"_, "Denormal"_};

/// Log
static Lock logLock;
template<> void log_(const ref<byte>& buffer) {/*Locker lock(logLock);*/ write(1,buffer.data,buffer.size); }
template<> void log(const ref<byte>& buffer) { log_(buffer+"\n"_); }
static ref<byte> message;
template<> void __attribute((noreturn)) error(const ref<byte>& buffer) { message=buffer; tgkill(getpid(),gettid(),SIGABRT); for(;;) pause(); }

/// Semaphore
void Semaphore::wait(int& futex, int val) { int e; while((e=check__(::futex(&futex,FUTEX_WAIT,val,0,0,0))) /*|| (val=futex)<0*/)log(errno[-e]); }
void Semaphore::wake(int& futex) { check_(::futex(&futex,FUTEX_WAKE,1,0,0,0),futex); }

/// Lock
debug(void Lock::setOwner(){assert(!owner,owner); owner=gettid();})
debug(void Lock::checkRecursion(){assert(owner!=gettid(),owner,gettid());})
debug(void Lock::checkOwner(){assert(owner==gettid(),owner,gettid()); owner=0;})

/// Poll
Poll::Poll(int fd, int events, Thread& thread):____(pollfd{fd,(short)events},)thread(thread){ if(fd) { thread+=this; thread.post(); } }
Poll::~Poll() {Locker lock(thread.lock); thread.unregistered<<this;}
void Poll::queue() {{Locker lock(thread.lock); thread.queue+= this;} thread.post();}

/// EventFD
enum{EFD_SEMAPHORE=1};
EventFD::EventFD():Stream(eventfd2(0,0)){}

/// Thread
Thread::Thread():Poll(EventFD::fd,POLLIN,*this){}
static int run(void* thread) { return ((Thread*)thread)->run(); }
void Thread::spawn(int unused priority) {
    this->priority=priority;
    static constexpr int stackSize = 1<<20;
    void* stack = (void*)check(mmap(0,stackSize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    mprotect(stack,4096,0);
    clone(::run,(byte*)stack+stackSize,CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_IO,this);
}
static array<Thread*> threads; static Lock threadsLock;
Thread defaultThread;
int Thread::run() {
    tid=gettid();
    if(priority) check_(setpriority(0,0,priority));
    {Locker lock(threadsLock); threads<<this;}
    while(!terminate) {
        uint size=this->size(); pollfd pollfds[size]; for(uint i=0;i<size;i++) pollfds[i]=*at(i);
        if(check__(::poll(pollfds,size,-1))!=INTR) for(uint i=0;i<size;i++) {
            Poll* poll=at(i); int revents=pollfds[i].revents;
            if(revents && !unregistered.contains(poll)) { poll->revents = revents; poll->event(); }
        }
        {Locker lock(thread.lock); while(unregistered){Poll* poll=unregistered.pop(); removeAll(poll); queue.removeAll(poll);}}
    }
    return 0;
}
void Thread::event(){
    EventFD::read();
    if(queue){
        Poll* poll;
        {Locker lock(thread.lock);
            poll=queue.take(0);
            if(unregistered.contains(poll)) return;
        }
        poll->revents=IDLE;
        poll->event();
    }
}

/// Signal handler
static void handler(int sig, siginfo* info, ucontext* ctx) {
    extern string trace(int skip, void* ip);
    string s = trace(sig==SIGABRT?3:2,sig==SIGABRT?0:(void*)ctx->ip);
    if(threads.size()>1) log_("Thread #"_+dec(gettid())+":\n"_+s); else log(s);
    if(sig!=SIGTRAP){
        Locker lock(threadsLock);
        for(Thread* thread: threads) {thread->terminate=true; if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP);}
    }
    if(sig==SIGABRT) { log("Abort",message); exit(0); } //Abort doesn't let thread terminate cleanly
    if(sig==SIGFPE) { log("Floating-point exception (",fpErrors[info->code],")", *(float*)info->fault.addr); }
    if(sig==SIGSEGV) { log("Segmentation fault at "_+str(ptr(info->fault.addr))); exit(0); } //Segfault kills the threads to prevent further corruption
    if(sig==SIGTERM) log("Terminated"); // Any signal (except trap) tries to cleanly terminate all threads
}
extern void restore_rt (void) asm ("__restore_rt"); asm(".text\n.align 16\n__restore_rt:\nmovq $15, %rax\nsyscall\n");
void init() {
    /// Limit stack size to avoid locking system by exhausting memory with recusive calls
    rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit);
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct {
        void (*sigaction) (int, struct siginfo*, ucontext*) = &handler;
        enum { SA_SIGINFO=4, SA_RESTORER=0x4000000, SA_RESTART=0x10000000 }; long flags = SA_SIGINFO|SA_RESTORER|SA_RESTART;
        void (*restorer)() = &restore_rt;
        uint mask[2] = {0,0};
    } sa;
    check_(sigaction(SIGABRT, &sa, 0, 8));
    check_(sigaction(SIGFPE, &sa, 0, 8));
    check_(sigaction(SIGSEGV, &sa, 0, 8));
    check_(sigaction(SIGTERM, &sa, 0, 8));
    check_(sigaction(SIGTRAP, &sa, 0, 8));
}
void exit() { for(Thread* thread: threads) thread->terminate=true; exit_group(0); /*TODO: allow each thread to terminate properly instead of killing with exit_group*/ }

/// Scheduler
void yield() { sched_yield(); }

/// Environment

void execute(const ref<byte>& path, const ref<string>& args, bool wait) {
    if(!existsFile(path)) { warn("Executable not found",path); return; }

    array<stringz> args0(1+args.size);
    args0 << strz(path);
    for(uint i=0;i<args.size;i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i];
    argv[args0.size()]=0;

    array< ref<byte> > env0;
    static string environ = File("proc/self/environ"_).readUpTo(4096);
    for(TextData s(environ);s;) env0<<s.until('\0');

    const char* envp[env0.size()+1];
    for(uint i=0;i<env0.size();i++) envp[i]=env0[i].data;
    envp[env0.size()]=0;

    int pid = fork();
    if(pid==0) { if(!execve(strz(path),argv,envp)) exit(-1); }
    else if(wait) wait4(pid,0,0,0);
}

ref<byte> getenv(const ref<byte>& name) {
    static string environ = File("proc/self/environ"_).readUpTo(4096);
    for(TextData s(environ);s;) {
        ref<byte> key=s.until('='); ref<byte> value=s.until('\0');
        if(key==name) return value;
    }
    warn("Undefined environment variable"_, name);
    return ""_;
}

array< ref<byte> > arguments() {
    static string arguments = File("proc/self/cmdline"_).readUpTo(4096);
    return split(section(arguments,0,1,-1),0);
}

const Folder& home() { static Folder home=getenv("HOME"_); return home; }
const Folder& config() { static Folder config=Folder(".config"_,home(),true); return config; }
const Folder& cache() { static Folder cache=Folder(".cache"_,home(),true); return cache; }
