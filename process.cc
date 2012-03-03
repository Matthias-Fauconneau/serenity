#include "process.h"
#include "map.h"
//#include "file.h"

#include <poll.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sched.h>
extern "C" __pid_t waitpid(__pid_t __pid, int *__stat_loc, int __options);

#define declare(function, attributes...) function __attribute((attributes)); function

/// Process

void setPriority(int priority) { setpriority(PRIO_PROCESS,0,priority); }

/// limit process ressources to avoid hanging the system when debugging
declare(static void limit_resource(), constructor) {
    /*{ rlimit limit; getrlimit(RLIMIT_STACK,&limit); limit.rlim_cur=1<<21; setrlimit(RLIMIT_STACK,&limit); } //2M
    { rlimit limit; getrlimit(RLIMIT_DATA,&limit); limit.rlim_cur=1<<24; setrlimit(RLIMIT_DATA,&limit); } //16M
    { rlimit limit; getrlimit(RLIMIT_AS,&limit); limit.rlim_cur=1<<28; setrlimit(RLIMIT_AS,&limit); } //256M*/
    setPriority(19);
}

int getCPUTime() {
    rusage usage; getrusage(RUSAGE_SELF,&usage);
    return  usage.ru_stime.tv_sec*1000+usage.ru_stime.tv_usec/1000 + //user time in ms
            usage.ru_utime.tv_sec*1000+usage.ru_utime.tv_usec/1000; //kernel time in ms
}

void execute(const string& path, const array<string>& args) {
    array<string> args0(1+args.size());
    args0 << copy(strz(path));
    for(int i=0;i<args.size();i++) args0 << copy(strz(args[i]));
    const char* argv[args0.size()+1];
    for(int i=0;i<args0.size();i++) argv[i]=&args0[i];
    argv[1+args.size()]=0;
    pid_t pid = fork();
    if(pid==0) {
        unshare(CLONE_FILES);
        if(!execv(&strz(path),(char* const*)argv)) _exit(-1);
    }
    //int status; waitpid(pid,&status,0);
}

/// Poll

static map<Poll*,pollfd> polls __attribute((init_priority(103)));
void Poll::registerPoll() { polls.insert(this,this->poll()); }
void Poll::unregisterPoll() { polls.remove(this); }

/// Application

Application* app=0;
Application::Application() { assert(!app,"Multiple application compiled in executable"_); app=this; }
int main(int argc, const char** argv) {
    {
        array<string> args;
        for(int i=1;i<argc;i++) args << strz(argv[i]);
        assert(app,"No application compiled in executable"_);
        app->start(move(args));
    }
    while(polls.size() && app->running) {
        ::poll((pollfd*)&polls.values,polls.size(),-1);
        for(int i=0;i<polls.size();i++) if(polls.values[i].revents) polls.keys[i]->event(polls.values[i]);
    }
    return 0;
}

#ifdef DEBUG

/// Debug symbols

#include <bfd.h>
#include <cxxabi.h>

static bfd* abfd;
static void* syms;
declare(static void read_debug_symbols(), constructor(101)) {
    bfd_init();
    abfd = bfd_openr("/proc/self/exe",0);
    assert(!bfd_check_format(abfd, bfd_archive));
    char** matching; assert(bfd_check_format_matches(abfd, bfd_object, &matching));
    if ((bfd_get_file_flags(abfd) & HAS_SYMS) != 0) {
        unsigned int size=0;
        long symcount = bfd_read_minisymbols(abfd, false, &syms, &size);
        if(symcount == 0) symcount = bfd_read_minisymbols(abfd, true, &syms, &size);
        assert(symcount >= 0);
    }
}
struct Symbol { string file,function; uint line; };
Symbol findNearestLine(void* address) {
    for(bfd_section* s=abfd->sections;s;s=s->next) {
        if((bfd_vma)address < s->vma || (bfd_vma)address >= s->vma + s->size) continue;
        const char* path=0; const char* func=0; uint line=0;
        if(bfd_find_nearest_line(abfd, s, (bfd_symbol**)syms, (bfd_vma)address - s->vma, &path, &func, &line)) {
            if(!path || !func || !line) continue;
            static size_t length=128; static char* buffer=(char*)malloc(length); int status;
            buffer=abi::__cxa_demangle(func,buffer,&length,&status);
            return i({ section(strz(path),'/',-2,-1), strz(!status?buffer:func), (int)line });
        }
    }
    return i({string(),string(),0});
}

/// Stack

struct StackFrame {
    StackFrame* caller_frame; void* return_address;
    static inline StackFrame* current() { register StackFrame* ebp asm("ebp"); return ebp; }
};
int backtrace(void** frames, int capacity, StackFrame* ebp) {
    int i=0;
    for(;i<capacity;i++) {
        frames[i]=ebp->return_address;
        if(int64(ebp=ebp->caller_frame)<0x10) break;
    }
    return i;
}
void logBacktrace(StackFrame* frame) {
    void* frames[8];
    int size = backtrace(frames,8,frame);
    for(int i=size-1; i>=0; i--) {
        Symbol s = findNearestLine(frames[i]);
        if(s.function && s.function[0]!='_') { log(s.file+":"_+str(s.line)+"   \t"_+s.function); }
    }
}

#ifdef TRACE

/// Call Trace

bool trace_enable = false;
struct Trace {
    // trace ring buffer
    void** buffer;
    int size=256;
    int index=0;
    // keep call stack leading to beginning of trace ring buffer
    array<void*> stack;

    Trace() { buffer=new void*[size]; clear(buffer,size); }
    void trace(void* function) {
        for(int loopSize=1;loopSize<=16;loopSize++) { //foreach loop size
            int depth=0;
            for(int i=loopSize;i>0;i--) { //find loop
                if(buffer[(index+size-i)%size]/*current*/ != buffer[(index+size-i-loopSize)%size]/*previous iteration*/) {
                    goto mismatch;
                }
                if(buffer[(index+size-i)%size]) depth++; else depth--;
                if(depth<0) goto mismatch;
            }
            if(depth!=0) goto mismatch;
            //found loop, erase repetition
            index = (index+size-loopSize)%size;
            break;
            mismatch: ;
        }
        void* last = buffer[index];
        if(last) stack << last;
        else if(stack.size) stack.removeLast();
        buffer[index++] = function;
        if(index==size) index=0;
    }
    void log() {
        trace_off;
        int depth=0;
        for(;depth<stack.size-1;depth++) {
            Symbol s = findNearestLine(stack[depth]);
            for(int i=0;i<depth;i++) ::log_(' ');
            ::log(s.function);
        }
        for(int i=0;i<size;i++) {
            void* function = buffer[index++];
            if(index==size) index=0;
            if(!function) { depth--; continue; } else depth++;
            Symbol s = findNearestLine(function);
            for(int i=0;i<depth-1;i++) ::log_(' ');
            ::log(s.function);
        }
    }
} trace __attribute((init_priority(102)));

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

void logTrace() { trace.log(); logBacktrace(StackFrame::current()->caller_frame); }
#else
void logTrace() { logBacktrace(StackFrame::current()->caller_frame); }
void logTrace(int skip) {
    StackFrame* frame = StackFrame::current();
    while(skip--) frame=frame->caller_frame;
    logBacktrace(frame);
}
#ifdef PROFILE
/// Profiler

bool trace_enable = false;
struct Trace {
    array<void*> stack;
    array<int> enter;
    map<void*,int> profile;

    Trace() { trace_enable=true; }
    void trace(void* function) {
        if(function) {
            stack << function;
            enter << getCPUTime();
        } else if(stack) {
            void* function = stack.pop();
            int time = getCPUTime()-enter.pop();
            for(void* e: stack) if(function==e) return; //profile only topmost recursive
            profile[function] += time;
        }
    }
} trace __attribute((init_priority(102)));

#define no_trace(function) declare(function,no_instrument_function)
no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

void logProfile() {
    trace_off;
    for(auto e: trace.profile) {
        if(e.value>40) log(toString(e.value)+"\t"_+findNearestLine(e.key).function);
    }
    trace_on;
}
#endif
#endif

/// Signal handler

#define signal _signal
#include <signal.h>
#undef signal

#include <fenv.h>

enum SW { IE = 1, DE = 2, ZE = 4, OE = 8, UE = 16, PE = 32 };
static void handler(int sig, siginfo*, void* ctx) {
    ucontext* context = (ucontext*)ctx;
    if(sig == SIGSEGV) log("Segmentation violation"_);
    else if(sig == SIGPIPE) log("Broken Pipe"_);
    else if(sig == SIGFPE) {
        log("Arithmetic exception ");
#if __WORDSIZE == 64
		const string flags[] = {"Invalid Operand"_,"Denormal Operand"_,"Zero Divide"_,"Overflow"_,"Underflow"_};
        string s;
        for(int i=1;i<=4;i++) if(context->uc_mcontext.fpregs->mxcsr & (1<<i)) s<<flags[i]+" "_;
        log(s);
#endif
    }
    else error("Unhandled signal"_);
#if __WORDSIZE == 64
    logBacktrace((StackFrame*)(context->uc_mcontext.gregs[REG_RBP]));
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_RIP]);
#else
	logBacktrace((StackFrame*)(context->uc_mcontext.gregs[REG_EBP]));
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_EIP]);
#endif
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);
    log("Aborted"_); abort();
}

declare(static void catch_sigsegv(), constructor) {
    struct sigaction sa; clear(sa);
    sa.sa_sigaction = &handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGPIPE, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
    feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW);
}

#endif
