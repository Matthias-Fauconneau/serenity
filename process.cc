#include "process.h"
#include "linux.h"
#include "array.cc"
static array<Poll*> polls;
static array<pollfd> pollfds;

void setupHeap(); //memory.cc
void catchErrors(); //debug.cc
enum { RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS };
struct rlimit { ulong cur,max; };

array<string> init_(int argc, char** argv) {
    setupHeap(); catchErrors();
    rlimit limit = {1<<20,1<<20}; setrlimit(RLIMIT_STACK,&limit); //1 MB
    array<string> args; for(int i=1;i<argc;i++) args << str(*(argv-i));
    return args;
}
void exit_(int code) { exit(code); }

void Poll::registerPoll(pollfd fd) { polls << this; pollfds << fd; }
void Poll::unregisterPoll() { int i=removeOne(polls, this); if(i>=0) pollfds.removeAt(i); }
static array<Poll*> queue;
void Poll::wait() { queue << this; }

int dispatchEvents() {
    if(!polls) return 0;
    ::poll((pollfd*)pollfds.data(),polls.size(),-1);
    for(uint i=0;i<polls.size();i++) {
        int events = pollfds[i].revents;
        if(events) {
            polls[i]->event(pollfds[i]);
            if(events&POLLHUP) { polls.removeAt(i); pollfds.removeAt(i); i--; continue; }
        }
    }
    while(queue) queue.takeFirst()->event(i({}));
    return polls.size();
}

void execute(const string& path, const array<string>& args) {
    array<CString> args0(1+args.size());
    args0 << strz(path);
    for(uint i=0;i<args.size();i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i].data;
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
