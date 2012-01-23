#include "process.h"

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
struct Symbol { string file,function; int line; };
Symbol findNearestLine(void* address) {
	for(bfd_section* s=abfd->sections;s;s=s->next) {
		if((bfd_vma)address < s->vma || (bfd_vma)address >= s->vma + s->size) continue;
        const char* path=0; const char* func=0; uint line=0;
        if(bfd_find_nearest_line(abfd, s, (bfd_symbol**)syms, (bfd_vma)address - s->vma, &path, &func, &line)) {
            if(!path || !func || !line) continue;
            string file = section(strz(path),'/',-2,-1);
            char* demangle = abi::__cxa_demangle(func,0,0,0);
            string function = strz(demangle?:func).copy();
            free(demangle);
            return {move(file),move(function),line};
		}
	}
    return {};
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
        if(s.function) { log<string>(s.file+":"_+toString(s.line)+"   \t"_+s.function); }
	}
}
//TODO: log local symbols?
void logFrame() {
    void* frames[8];
    int size = backtrace(frames,8,StackFrame::current()->caller_frame);
    for(int i=0; i<size; i++) {
        Symbol s = findNearestLine(frames[i]);
        if(s.function) { log<string>(s.file+":"_+toString(s.line)+"   "_+s.function); break; }
    }
}

#ifdef TRACE

/// Instrumentation

bool trace_enable = false;
struct Trace {
	// trace ring buffer
	void** buffer;
	int size=256;
	int index=0;
	// keep call stack leading to beginning of trace ring buffer
	array<void*> stack;

	Trace() { buffer=new void*[size]; clear(buffer,size); trace_enable=true; }
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

no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

void logTrace() { trace.log(); logBacktrace(StackFrame::current()->caller_frame); }
#else
void logTrace() { logBacktrace(StackFrame::current()->caller_frame); }
#endif

/// Signal handler

#define signal _signal
#include <signal.h>
#undef signal

enum SW { IE = 1, DE = 2, ZE = 4, OE = 8, UE = 16, PE = 32 };
no_trace(static void handler(int sig, siginfo*, void* ctx)) {
	trace_off;
	ucontext* context = (ucontext*)ctx;
    if(sig == SIGSEGV) log("Segmentation Violation"_);
    else if(sig == SIGPIPE) log("Broken pipe"_);
    else error("Unhandled signal"_);
#ifdef TRACE
    trace.log();
#endif
    logBacktrace((StackFrame*)(context->uc_mcontext.gregs[REG_RBP]));
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_RIP]);
    log_(s.file);log_(':');log_(s.line);log_("   \t"_);log(s.function);
    log("Aborted"_); abort();
}

declare(static void catch_sigsegv(), constructor) {
	struct sigaction sa; clear(sa);
	sa.sa_sigaction = &handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGPIPE, &sa, 0);
}

#endif

/// limit process ressources to avoid hanging the system when debugging
#include <sys/resource.h>
declare(static void limit_resource(), constructor) {
    { rlimit limit; getrlimit(RLIMIT_STACK,&limit); limit.rlim_cur=1<<8;/*64K*/ setrlimit(RLIMIT_STACK,&limit); }
    { rlimit limit; getrlimit(RLIMIT_DATA,&limit); limit.rlim_cur=1<<24;/*16M*/ setrlimit(RLIMIT_DATA,&limit); }
    { rlimit limit; getrlimit(RLIMIT_AS,&limit); limit.rlim_cur=1<<28;/*256M*/ setrlimit(RLIMIT_AS,&limit); }
    setpriority(PRIO_PROCESS,0,19);
}

/// Poll

#include "map.h"
static map<Poll*,pollfd> polls __attribute((init_priority(103)));
void Poll::registerPoll() { polls.insert(this,this->poll()); }
void Poll::unregisterPoll() { polls.remove(this); }

/// Application

Application* app=0;
Application::Application() { assert(!app,"Multiple application compiled in executable"_); app=this; }
int main(int argc, const char** argv) {
	array<string> args;
	for(int i=1;i<argc;i++) args << strz(argv[i]);
    assert(app,"No application compiled in executable"_);
	app->start(move(args));
    while(polls.size() && app->running) {
        ::poll((pollfd*)&polls.values,polls.size(),-1);
        for(int i=0;i<polls.size();i++) if(polls.values[i].revents) polls.keys[i]->event(polls.values[i]);
	}
	return 0;
}
