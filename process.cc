#include "process.h"
#include "linux.h"
#include "debug.h"
#include "file.h"
#include "stream.h"

void abort() { /*kill(0,SIGABRT); __builtin_unreachable();*/ exit(-1); }
void write(const ref<byte>& buffer) { write(1,buffer.data,buffer.size); }

struct siginfo { int signo,errno,code; struct { void *addr; } fault; };
struct ucontext {
    long flags; ucontext *link; void* ss_sp; int ss_flags; long ss_size;
#if __arm__
    long trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,eip,cpsr,fault;
#elif __x86_64__ || __i386__
    long gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,eip,cs,efl,uesp,ss;
#endif
};
Application* app;
static void handler(int sig, siginfo* info, ucontext* ctx) {
    ::write(1,"SIGNAL",6);
    log("SIGNAL"_);
    if(sig==SIGFPE) log("Floating-point exception");
    trace(1);
    {Symbol s = findNearestLine((void*)ctx->eip); log(s.file+":"_+str(s.line)+"     \t"_+s.function);}
    if(sig==SIGABRT) log("Abort");
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(ptr(info->fault.addr)));
    if(sig==SIGPIPE) log("Broken pipe");
    if(sig==SIGTERM) { log("Terminated"); app->running=false; return; }
    exit(-1);
}

#include "fenv.h"
#undef Application
Application::Application () {
    assert(!app); app=this;
    /// Limit stack size to avoid locking system by exhausting memory with recusive calls
    rlimit limit = {8<<20,8<<20}; setrlimit(RLIMIT_STACK,&limit); //8 MB
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct {
        void (*sigaction) (int, struct siginfo*, ucontext*) = &handler;
        enum { SA_SIGINFO=4 } flags = SA_SIGINFO;
        void (*restorer) (void) = 0;
        uint mask[2] = {0,0};
    } sa;
    sigaction(SIGABRT, &sa, 0, 8);
    sigaction(SIGFPE, &sa, 0, 8);
    sigaction(SIGSEGV, &sa, 0, 8);
    sigaction(SIGPIPE, &sa, 0, 8);
    sigaction(SIGTERM, &sa, 0, 8);
    feenableexcept(FE_DIVBYZERO|FE_INVALID);
}

static array<Poll*> polls;
void Poll::registerPoll(int events) { assert(!polls.contains(this)); assert(events); this->events=events; polls << this; }
static array<Poll*> unregistered;
void Poll::unregisterPoll() { events=revents=0; if(polls.removeAll(this)) unregistered<<this; }
static array<Poll*> queue;
void Poll::wait() { queue+= this; }
bool Poll::poll() { return ::poll(this,1,0)==1 && events; }

int pollEvents(pollfd* pollfds, uint& size, int timeout){size=min(size,polls.size()); for(uint i=0;i<size;i++) copy((byte*)&pollfds[i],(byte*)(pollfd*)polls[i],sizeof(pollfd)); return ::poll(pollfds,size,timeout);}
int dispatchEvents() {
    if(!polls) return 0;
    uint size=polls.size();
#if __clang__
    byte pollfds_[size*sizeof(pollfd)]; clear(pollfds_,sizeof(pollfds_)); pollfd* pollfds=(pollfd*)pollfds_;
#else
    pollfd pollfds[size];
#endif
    while(queue){Poll* poll=queue.take(0); poll->revents=IDLE; poll->event(); if(pollEvents(pollfds,size,0)) goto break_;}
    /*else*/ pollEvents(pollfds,size,-1);
    break_:;
    Poll* polls[size]; copy(polls,::polls.data(),size);
    for(uint i=0;i<size;i++) {
        Poll* poll=polls[i];
        if(!unregistered.contains(poll) && (poll->revents = pollfds[i].revents)) {
            poll->event();
        }
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
