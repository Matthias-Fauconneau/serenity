#include "process.h"
#include "array.cc"

Array_Copy_Compare(Poll*)
static array<Poll*> polls;
Array_Copy(pollfd)
static array<pollfd> pollfds;
void Poll::registerPoll(pollfd fd) { polls << this; pollfds << fd; }
void Poll::unregisterPoll() { int i=removeOne(polls, this); if(i>=0) pollfds.removeAt(i); }
static array<Poll*> queue;
void Poll::wait() { queue << this; }


int dispatchEvents(bool wait) {
    if(!polls.size()) return 0;
    ::poll((pollfd*)pollfds.data(),polls.size(),wait?-1:0);
    for(uint i=0;i<polls.size();i++) {
        int events = pollfds[i].revents;
        if(events) {
            if(!(events&POLLIN)) warn("!POLLIN"_);
            if(events&POLLHUP) { warn("POLLHUP"_); polls.removeAt(i); pollfds.removeAt(i); i--; continue; }
            polls[i]->event(pollfds[i]);
        }
    }
    for(Poll* p: queue) p->event(i({}));
    return polls.size();
}

extern "C" int fork();
extern "C" int execv(const char* path, char* const argv[]);
Array(CString)
void execute(const string& path, const array<string>& args) {
    array<CString> args0(1+args.size());
    args0 << strz(path);
    for(uint i=0;i<args.size();i++) args0 << strz(args[i]);
    const char* argv[args0.size()+1];
    for(uint i=0;i<args0.size();i++) argv[i]=args0[i].data;
    argv[args0.size()]=0;
    int pid = fork();
    if(pid==0) {
        if(!execv(strz(path),(char* const*)argv)) __builtin_abort();
    }
}

extern "C" int setpriority(int which, uint who, int prio);
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
        string key=s.until(":"_); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until("\n"_);
        info.insert(move(key), value);
    }
    return info.at("MemFree"_)+info.at("Inactive"_);
}
#endif
