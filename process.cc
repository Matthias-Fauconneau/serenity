#include "process.h"
#include "linux.h"
#include "debug.h"
#include "file.h"
#include "stream.h"

enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM };
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
    trace(1);
    {Symbol s = findNearestLine((void*)ctx->eip); log(s.file+":"_+str(s.line)+"     \t"_+s.function);}
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(ptr(info->fault.addr)));
    if(sig==SIGABRT) log("Abort");
    if(sig==SIGPIPE) log("Broken pipe");
    if(sig==SIGTERM) { log("Terminated"); app->running=false; return; }
    exit(-1);
}

#undef Application
Application::Application () {
    assert(!app); app=this;
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct {
        void (*sigaction) (int, struct siginfo*, ucontext*) = &handler;
        enum { SA_SIGINFO=4 } flags = SA_SIGINFO;
        void (*restorer) (void) = 0;
        uint mask[2] = {0,0};
    } sa;
    sigaction(SIGABRT, &sa, 0, 8);
    sigaction(SIGSEGV, &sa, 0, 8);
    sigaction(SIGTERM, &sa, 0, 8);
    sigaction(SIGPIPE, &sa, 0, 8);
    //rlimit limit = {2<<20,2<<20}; setrlimit(RLIMIT_STACK,&limit); //2 MB
}

static array<Poll*> polls;
void Poll::registerPoll(int fd, int events) { assert(!polls.contains(this)); this->fd=fd; this->events=events; polls << this; }
static array<Poll*> unregistered;
void Poll::unregisterPoll() { events=revents=0; if(polls.removeAll(this)) unregistered<<this; }
static array<Poll*> queue;
void Poll::wait() { queue+= this; }

int dispatchEvents() {
    if(!polls) return 0;
    while(queue){ Poll* poll=queue.takeFirst(); poll->revents=IDLE; poll->event(); }
    uint size=polls.size();
    pollfd pollfds[size]; for(uint i=0;i<size;i++) { pollfds[i]=*polls[i];  assert(polls[i]->fd==pollfds[i].fd); }
    ::poll(pollfds,size,-1);
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
    static string environ = ::readUpTo(openFile("proc/self/environ"_),4096);
    for(TextStream s(environ);s;) env0<<s.until('\0');

    const char* envp[env0.size()+1];
    for(uint i=0;i<env0.size();i++) envp[i]=env0[i].data;
    envp[env0.size()]=0;

    int pid = fork();
    if(pid==0) { if(!execve(strz(path),argv,envp)) exit(-1); }
    else if(wait) wait4(pid,0,0,0);
}

void setPriority(int priority) { setpriority(0,0,priority); }

ref<byte> getenv(const ref<byte>& name) {
    static string environ = ::readUpTo(openFile("proc/self/environ"_),4096);
    for(TextStream s(environ);s;) {
        ref<byte> key=s.until('='); ref<byte> value=s.until('\0');
        if(key==name) return value;
    }
    warn("Undefined environment variable"_, name);
    return ""_;
}
