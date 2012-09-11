#include "process.h"
#include "linux.h"
#include "file.h"
#include "data.h"
#include "debug.h"

void abort() { exit(-1); }
void write(const ref<byte>& buffer) { write(1,buffer.data,buffer.size); }

struct rlimit { long cur,max; };
enum {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS};

struct siginfo { int signo,errno,code; struct { void *addr; } fault; };
struct ucontext {
    long flags; ucontext *link; struct { void* sp; int flags; long size; } stack;
#if __x86_64__
    long r8,r9,r10,r11,r12,r13,r14,r15,rdi,rsi,rbp,rbx,rdx,rax,rcx,rsp,ip,efl,csgsfs,err,trap,oldmask,cr2;
#elif __arm__
    long trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,ip,cpsr,fault;
#elif  || __i386__
    long gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,ip,cs,efl,uesp,ss;
#endif
};

static constexpr ref<byte> fpErrors[] = {""_, "Integer division"_, "Integer overflow"_, "Division by zero"_, "Overflow"_, "Underflow"_, "Precision"_, "Invalid"_, "Denormal"_};
static Application* app;
static int idle;
static void handler(int sig, siginfo* info, ucontext* ctx) {
    if(!idle) {
        trace(1);
        {Symbol s = findNearestLine((void*)ctx->ip); if(s.function||s.file||s.line) log(s.file+":"_+str(s.line)+"     \t"_+s.function); else log(ptr((void*)ctx->ip)); }
    }
    if(sig==SIGABRT) log("Abort");
    if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->code],")", *(float*)info->fault.addr);
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(ptr(info->fault.addr)));
    if(sig==SIGPIPE) log("Broken pipe");
    if(sig==SIGTERM) { log("Terminated"); app->running=false; return; }
    exit(-1);
}

#undef Application
extern void restore_rt (void) asm ("__restore_rt");
Application::Application () {
    assert_(!app); app=this;
    /// Limit stack size to avoid locking system by exhausting memory with recusive calls
    rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit);
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct {
        void (*sigaction) (int, struct siginfo*, ucontext*) = &handler;
        enum { SA_SIGINFO=4, SA_RESTORER=0x4000000 }; long flags = SA_SIGINFO|SA_RESTORER;
        void (*restorer)() = &restore_rt;
        uint mask[2] = {0,0};
    } sa;
    check_(sigaction(SIGABRT, &sa, 0, 8));
    check_(sigaction(SIGFPE, &sa, 0, 8));
    check_(sigaction(SIGSEGV, &sa, 0, 8));
    check_(sigaction(SIGPIPE, &sa, 0, 8));
    check_(sigaction(SIGTERM, &sa, 0, 8));
}

static array<Poll*> polls;
static array<Poll*> queue;
static array<Poll*> unregistered;
Poll::Poll(int fd, int events) { assert_(fd); this->fd=fd; assert_(events); this->events=events; polls << this; }
Poll::~Poll() { polls.removeOne(this); unregistered<<this; assert_(!queue.contains(this)); }
void Poll::wait() { queue+= this; }

int pollEvents(pollfd* pollfds, uint& size, int timeout){size=min(size,polls.size()); for(uint i=0;i<size;i++) pollfds[i]=*polls[i]; return ::poll(pollfds,size,timeout);}
int dispatchEvents() {
    if(!polls) return 0;
    uint size=polls.size();
    pollfd pollfds[size];
    while(queue){Poll* poll=queue.take(0); poll->revents=IDLE; poll->event(); if(pollEvents(pollfds,size,0)) goto break_;}
    /*else*/{idle=1; pollEvents(pollfds,size,-1); idle=0;}
    break_:;
    Poll* polls[size]; copy(polls,::polls.data(),size);
    for(uint i=0;i<size;i++) {
        Poll* poll=polls[i]; int revents=pollfds[i].revents;
        if(revents && !unregistered.contains(poll)) { poll->revents = revents; poll->event(); }
    }
    unregistered.clear();
    return ::polls.size();
}

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

void setPriority(int priority) { check_(setpriority(0,0,priority)); }

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

asm(".text\n.align 16\n__restore_rt:\nmovq $15, %rax\nsyscall\n");
