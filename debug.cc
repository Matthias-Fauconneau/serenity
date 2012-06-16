#include "debug.h"

#ifdef DEBUG

/// Debug symbols

#include <bfd.h>
#include <cxxabi.h>

Symbol findNearestLine(void* address) {
    static bfd* bfd = 0;
    static void* syms = 0;
    if(!bfd) {
        bfd_init();
        bfd = bfd_openr("/proc/self/exe",0);
        assert(!bfd_check_format(bfd, bfd_archive));
        char** matching; assert(bfd_check_format_matches(bfd, bfd_object, &matching));
        if ((bfd_get_file_flags(bfd) & HAS_SYMS) != 0) {
            unsigned int size=0;
            long symcount = bfd_read_minisymbols(bfd, false, &syms, &size);
            if(symcount == 0) symcount = bfd_read_minisymbols(bfd, true, &syms, &size);
            assert(symcount >= 0);
        }
    }
    for(bfd_section* s=bfd->sections;s;s=s->next) {
        if((bfd_vma)address < s->vma || (bfd_vma)address >= s->vma + s->size) continue;
        const char* path=0; const char* func=0; uint line=0;
        if(bfd_find_nearest_line(bfd, s, (bfd_symbol**)syms, (bfd_vma)address - s->vma, &path, &func, &line)) {
            if(!path || !func || !line) continue;
            static size_t length=128; static char* buffer=(char*)malloc(length); int status;
            buffer=abi::__cxa_demangle(func,buffer,&length,&status);
            return i({section(strz(path),'/',-2,-1), strz(!status?buffer:func), (int)line});
        }
    }
    return i({string(),string(),0});
}

/// Stack


#if __x86_64__ || __i386__
void* caller_frame(void* fp) { return *(void**)fp; }
void* return_address(void* fp) { return *((void**)fp+1); }
#elif __arm__
void* caller_frame(void* fp) { return *((void**)fp-3); }
void* return_address(void* fp) { return *((void**)fp-1); }
#else
#error Unsupported architecture
#endif

void logBacktrace(void* frame) {
    void* frames[8];
    int i=0;
    for(;i<8;i++) {
        if(!frame) break;
        frames[i]=return_address(frame);
        frame=caller_frame(frame);
    }
    for(i--; i>=0; i--) {
        Symbol s = findNearestLine(frames[i]);
        if(s.function && s.function[0]!='_') { log(s.file+":"_+str(s.line)+"   \t"_+s.function); }
    }
}
void logTrace(int /*skip*/) {
    void* frame = __builtin_frame_address(0);
    //while(skip-- && uint64(frame->caller_frame)<0x10 && uint64(frame->caller_frame)>0x10000000000000) frame=frame->caller_frame;
    logBacktrace(frame);
}

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
        log("Arithmetic exception "_);
#if __x86_64__ || __i386__
        const string flags[] = {"Invalid Operand"_,"Denormal Operand"_,"Zero Divide"_,"Overflow"_,"Underflow"_};
        string s;
        for(int i=0;i<=4;i++) if(context->uc_mcontext.fpregs->mxcsr & (1<<i)) s<<flags[i]+" "_;
        log(s);
#endif
    }
#if __x86_64__
    logBacktrace(context->uc_mcontext.gregs[REG_RBP]);
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_RIP]);
#elif __i386__
    logBacktrace(context->uc_mcontext.gregs[REG_EBP]);
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_EIP]);
#elif __arm__
    logBacktrace((void*)context->uc_mcontext.arm_fp);
    Symbol s = findNearestLine((void*)context->uc_mcontext.arm_ip);
#else
#error Unsupported architecture
#endif
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);
    __builtin_abort();
}

static_this() {
    struct sigaction sa; clear(sa);
    sa.sa_sigaction = &handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGPIPE, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
    feenableexcept(FE_DIVBYZERO|FE_INVALID);
}

#endif
