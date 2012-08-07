#include "process.h"
#include "linux.h"
#include "array.cc"

void setupHeap(); //memory.cc

/// Disassemble 32-bit ARM instructions
struct ARM {
    union { //reversed bytes for little-endian
        //struct { uint cond:4,_00:2,I:1,opcode:4,S:1,Rn:4,Rd:4,operand2:16;} data; //Data processing / PSR transfer
        //struct { uint cond:4,_000000:6,A:1,S:1,Rd:4,Rn:4,Rs:4,_1001:4,Rm:4;} multiply; //Multiply
        struct { uint _1001:4,Rm:4,Rn:4,Rs:4,cond:4,_000000:6,A:1,S:1,Rd:4;} multiply; //Multiply
        //struct { uint cond:4,_00010:5,B:1,_00:2,Rn:4,Rd:4,_000010001:8,Rm:4;} swap; //Swap
        //struct { uint cond:4,_01:2,I:1,P:1,U:1,B:1,W:1,L:1,Rn:4,Rd:4,offset:12;} ls; //Load/Store
        //struct { uint Rd:4,offset:12,U:1,B:1,W:1,L:1,Rn:4,_01:2,I:1,P:1,cond:4;} ls; //Load/Store
        struct { uint offset:12,Rd:4,Rn:4,L:1,W:1,B:1,U:1,P:1,I:1,_01:2,cond:4; } ls; //Load/Store
        //struct { uint cond:4,_100:3,P:1,U:1,S:1,W:1,L:1,Rn:4,registers:16;} lsm; //Load/Store multiple
        //struct { uint cond:4,_101:3,L:1,offset:24;} branch; //Branch
        struct { uint code:28,cond:4; } unknown;
    };
    operator string() {
        if(multiply._000000==0b000000) return string("Thumb"_);
        else if(ls._01==0b01) return str(ls.W?"str"_:"ldr"_,"r"_+dec(ls.Rd)+","_,"[r"_+dec(ls.Rn)+"]"_);
        else error("Unknown instruction",bin(*(uint*)this,32),bin(unknown.cond,4),bin(ls.cond,4),bin(unknown.code,28),bin(ls._01,2));
    }
};
static_assert(sizeof(ARM)==4,"invalid bitfield");
string disasm(ulong ptr) { return string(*(ARM*)ptr); }

enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM };
struct siginfo { int signo,errno,code; struct { void *addr; } fault; };
struct ucontext {
    ulong flags; ucontext *link; void* ss_sp; int ss_flags; ulong ss_size;
#if __arm__
    ulong trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,pc,cpsr,fault;
#elif __x86_64__ || __i386__
    ulong gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,eip,cs,efl,uesp,ss;
#endif
};

static void handler(int signo, siginfo* info, ucontext* ctx) {
    trace(1);
#if __arm__
    {Symbol s = findNearestLine((void*)ctx->pc);  log(s.file+":"_+str(s.line)+"     \t"_+s.function);}
#elif __x86_64__ || __i386__
    {Symbol s = findNearestLine((void*)ctx->eip); log(s.file+":"_+str(s.line)+"     \t"_+s.function);}
#endif
    if(signo==SIGSEGV) log("Segmentation fault",ptr(info->fault.addr));
    abort();
}

void init_() {
    extern byte *heapEnd, *systemEnd;
    systemEnd = heapEnd = (byte*)brk(0);
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
void exit_(int code) { exit(code); } //TODO: check leaks

//FIXME: parallel arrays is bad for realloc TODO: put pollfd in Poll, and use stack array for poll(pollfd[])
static array<Poll*> polls;
static array<pollfd> pollfds;
void Poll::registerPoll(pollfd fd) { polls << this; pollfds << fd; }
static Poll* lastUnregistered; //for correct looping
void Poll::unregisterPoll() { for(int i;(i=removeOne(polls, this))>=0;) pollfds.removeAt(i); lastUnregistered=this; }
bool operator ==(pollfd a, pollfd b) { return a.fd==b.fd; }
void Poll::unregisterPoll(int fd) { int i=removeOne(pollfds, pollfd i({fd})); if(i>=0) polls.removeAt(i); }
static array<Poll*> queue;
void Poll::wait() { queue+= this; }

int dispatchEvents() {
    if(!polls) return 0;
    ::poll((pollfd*)pollfds.data(),polls.size(),-1);
    for(uint i=0;i<polls.size();i++) {
        int events = pollfds[i].revents;
        if(events) {
            lastUnregistered=0;
            polls[i]->event(pollfds[i]);
            if(i==polls.size() || polls[i]==lastUnregistered) i--;
            else if(events&POLLHUP) { log("POLLHUP"); polls.removeAt(i); pollfds.removeAt(i); i--; continue; }
        }
    }
    while(queue) queue.takeFirst()->event(i({}));
    return polls.size();
}

void execute(const string& path, const array<string>& args) {
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

#if PROCFS
#include "map.h"
#include "file.h"
#include "stream.h"
uint availableMemory() {
    TextBuffer s = ::readUpTo(openFile("/proc/meminfo"_),2048);
    close(fd);
    map<string, uint> info;
    while(s) {
        string key=s.until(':'); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until('\n');
        info.insert(move(key), value);
    }
    return info.at("MemFree"_)+info.at("Inactive"_);
}
#endif
