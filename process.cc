#include "process.h"
#include "linux.h"
#include "array.cc"

void setupHeap(); //memory.cc

static void handler(int, struct siginfo*, struct ucontext*) { trace(0); abort(); }

void init_() {
    extern byte *heapEnd, *systemEnd;
    systemEnd = heapEnd = (byte*)brk(0);
    enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM };
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
    rlimit limit = {1<<21,1<<21}; setrlimit(RLIMIT_STACK,&limit); //1 MB
}
void exit_(int code) { exit(code); } //TODO: check leaks

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
    int fd = openFile("/proc/meminfo"_);
    TextBuffer s = ::readUpTo(fd,2048);
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
