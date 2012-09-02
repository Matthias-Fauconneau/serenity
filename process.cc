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

static void handler(int sig, siginfo* info, ucontext* ctx) {
    if(sig==SIGABRT) log("Abort");
    trace(1);
    {Symbol s = findNearestLine((void*)ctx->eip); log(s.file+":"_+str(s.line)+"     \t"_+s.function);}
    if(sig==SIGSEGV) log("Segmentation fault at "_+str(ptr(info->fault.addr)));
    exit(-1);
}

void init() {
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM.PIPE}
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
void Poll::registerPoll(int fd, short events) { assert(!polls.contains(this)); this->fd=fd; this->events=events; polls << this; }
void Poll::registerPoll(short events) { registerPoll(fd,events); }
static int currentPoll; //correct looping when unregistering from event loop
void Poll::unregisterPoll() { int i=polls.indexOf(this); if(i==-1) return; polls.removeAt(i);  if(i<=currentPoll) currentPoll--; }
static array<Poll*> queue;
void Poll::wait() { queue+= this; }

int dispatchEvents() {
    if(!polls) return 0;
    while(queue){ Poll* poll=queue.takeFirst(); poll->revents=IDLE; poll->event(); }
    uint size=polls.size();
    pollfd pollfds[size]; for(uint i=0;i<size;i++) { pollfds[i]=*polls[i];  assert(polls[i]->fd==pollfds[i].fd); }
    ::poll(pollfds,size,-1);
    currentPoll=0; for(uint i=0;i<size;i++,currentPoll++) { Poll* poll=polls[currentPoll]; assert(poll->fd==pollfds[i].fd);
        int events = poll->revents = pollfds[i].revents;
        if(events) {
            poll->event();
            if(events&POLLHUP) poll->unregisterPoll();
        }
    }
    return polls.size();
}

void execute(const ref<byte>& path, const array<string>& args) {
    array<stringz> args0(1+args.size());
    args0 << strz(path);
    for(uint i=0;i<args.size();i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i];
    argv[args0.size()]=0;
    int pid = fork();
    if(pid==0) if(!execve(strz(path),argv,0)) exit(-1);
}

void setPriority(int priority) { setpriority(0,0,priority); }

ref<byte> getenv(const ref<byte>& name) {
    static string environ = ::readUpTo(openFile("proc/self/environ"_),4096);
    for(TextStream s(environ);s;) {
        ref<byte> key=s.until('='); ref<byte> value=s.until('\0');
        if(key==name) return value;
    }
    error("Undefined environment variable"_, name);
    return ""_;
}
