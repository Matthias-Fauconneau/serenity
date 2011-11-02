#include "process.h"
#include <bfd.h>
#include <cxxabi.h>
#include <signal.h>

/// Stack

struct StackFrame {
	StackFrame* caller_frame; void* return_address;
	static inline StackFrame* current() { register StackFrame* ebp asm("ebp"); return ebp; }
};

array<void*> backtrace(StackFrame* ebp) {
	array<void*> frames;
	do { frames << ebp->return_address; } while((ebp=ebp->caller_frame));
	return frames;
}

/// Debug symbols

struct Debug {
	bfd* abfd;
	void* syms;
	Debug() {
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
	struct Symbol { string file,function; int line=0; };
	Symbol findNearestLine(void* address) {
		for(bfd_section* s=abfd->sections;s;s=s->next) {
			if((bfd_vma)address < s->vma || (bfd_vma)address >= s->vma + s->size) continue;
			const char* path=0; const char* function=0; uint line=0;
			if(bfd_find_nearest_line(abfd, s, (bfd_symbol**)syms, (bfd_vma)address - s->vma, &path, &function, &line)) {
				if(!path || !function || !line) continue;
				Symbol symbol;
				symbol.file=section(strz(path),'/',-2,-1);
				if(symbol.file.startsWith(_("functional"))) break;
				char* demangle = abi::__cxa_demangle(function,0,0,0);
				if(demangle) { symbol.function=strz(demangle); symbol.function.detach(); free(demangle); }
				else symbol.function = strz(function);
				symbol.line=line;
				return symbol;
			}
		}
		Symbol missing; return missing;
	}
} debug;

void logBacktrace(StackFrame* frame) {
	array<void*> frames = backtrace(frame);
	for(int i=frames.size-4; i>=0; i--) {
		Debug::Symbol s = debug.findNearestLine(frames[i]);
		log_(s.file);log_(':');log_(s.line);log_(_("   \t"));log(s.function);
	}
}
void logBacktrace() { logBacktrace(StackFrame::current()); }

/// Instrumentation

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
		for(int loopSize=1;loopSize<=8;loopSize++) { //foreach loop size
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
			Debug::Symbol s = debug.findNearestLine(stack[depth]);
			for(int i=0;i<depth;i++) ::log_(' ');
			::log(s.function);
		}
		for(int i=0;i<size;i++) {
			void* function = buffer[index++];
			if(index==size) index=0;
			if(!function) { depth--; continue; } else depth++;
			Debug::Symbol s = debug.findNearestLine(function);
			for(int i=0;i<depth-1;i++) ::log_(' ');
			::log(s.function);
		}
	}
} trace;

no_trace(extern "C" void __cyg_profile_func_enter(void* function, void*)) { if(trace_enable) { trace_off; trace.trace(function); trace_on; }}
no_trace(extern "C" void __cyg_profile_func_exit(void*, void*)) { if(trace_enable) { trace_off; trace.trace(0); trace_on; } }

enum SW { IE = 1, DE = 2, ZE = 4, OE = 8, UE = 16, PE = 32 };
no_trace(static void handler(int sig, siginfo*, void* ctx)) {
	trace_off;
	ucontext* context = (ucontext*)ctx;
	if(sig == SIGSEGV) { log(_("Segmentation Fault: "));
		trace.log();
		logBacktrace((StackFrame*)(context->uc_mcontext.gregs[REG_RBP]));
		Debug::Symbol s = debug.findNearestLine((void*)context->uc_mcontext.gregs[REG_RIP]);
		log_(s.file);log_(':');log_(s.line);log_(_("   \t"));log(s.function);
	} else fail("Unhandled signal");
	log(_("Aborted")); abort();
}

declare(static void static_this(), constructor) {
	struct sigaction sa; clear(sa);
	sa.sa_sigaction = &handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, 0);
}

/// Poll

static array<Poll*> polls;
void Poll::registerPoll() { polls.appendOnce((Poll*)this); }
void Poll::unregisterPoll() { polls.removeOne((Poll*)this); }

/// Application

Application* app=0;
Application::Application() { assert(!app,"Multiple application compiled in executable"); app=this; }
int main(int argc, const char** argv) {
	trace_on;
	array<string> args;
	for(int i=1;i<argc;i++) args << strz(argv[i]);
	assert(app,"No application compiled in executable");
	app->start(move(args));
	pollfd pollfds[polls.size];
	for(int i=0;i<polls.size;i++) pollfds[i] = polls[i]->poll();
	for(;;) {
		assert(::poll(pollfds,polls.size,-1)>0,"poll");
		for(int i=0;i<polls.size;i++) if(pollfds[i].revents) if(!polls[i]->event(pollfds[i])) return 0;
	}
	return 0;
}
