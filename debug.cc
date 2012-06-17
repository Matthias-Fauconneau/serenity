#include "debug.h"

#if BFD
//#include <bfd.h>
//#include <cxxabi.h>

extern "C" {
struct bfd_hash_table {
    void* private1[3];
    unsigned int private2[4];
};
struct bfd {
  uint id;
  const char *filename;
  const struct bfd_target *xvec;
  void *iostream;
  const struct bfd_iovec *iovec;
  struct bfd *lru_prev, *lru_next;
  long long where;
  long mtime;
  int ifd;
  int format;
  int direction;
  int flags;
  long long origin;
  long long proxy_origin;
  struct bfd_hash_table section_htab;
  struct bfd_section *sections;
  struct bfd_section *section_last;
  unsigned int section_count;
  unsigned int symcount;
};
struct bfd_section {
  const char *name;
  int id;
  int index;
  struct bfd_section *next;
  struct bfd_section *prev;
  uint flags;
  unsigned int user_set_vma : 1;
  unsigned int linker_mark : 1;
  unsigned int linker_has_input : 1;
  unsigned int gc_mark : 1;
  unsigned int compress_status : 2;
  unsigned int segment_mark : 1;
  unsigned int sec_info_type:3;
  unsigned int use_rela_p:1;
  unsigned int sec_flg0:1;
  unsigned int sec_flg1:1;
  unsigned int sec_flg2:1;
  unsigned int sec_flg3:1;
  unsigned int sec_flg4:1;
  unsigned int sec_flg5:1;
  ulong vma;
  ulong lma;
  ulong size;
};
struct bfd_target {
  char *name;
  int private1[6];
  void* vtable[57];
  bool (*_bfd_find_nearest_line)(bfd *, struct bfd_section *, struct bfd_symbol **, ulong,
     const char **, const char **, unsigned int *);
  void* vtable2[3];
  long        (*_read_minisymbols) (bfd *, bool, void **, unsigned int *);
};
void bfd_init();
bfd*bfd_openr(const char* filename, const char* target);
#define BFD_SEND(bfd, message, arglist) ((*((bfd)->xvec->message)) arglist)
#define bfd_read_minisymbols(b, d, m, s) BFD_SEND (b, _read_minisymbols, (b, d, m, s))
#define bfd_find_nearest_line(abfd, sec, syms, off, file, func, line) \
       BFD_SEND (abfd, _bfd_find_nearest_line, (abfd, sec, syms, off, file, func, line))
}

static bfd* bfd = 0;
static void* syms = 0;

Symbol findNearestLine(void* address) {
    for(bfd_section* s=bfd->sections;s;s=s->next) {
        if((ulong)address < s->vma || (ulong)address >= s->vma + s->size) continue;
        const char* path=0; const char* symbol=0; uint line=0;
        if(bfd_find_nearest_line(bfd, s, (bfd_symbol**)syms, (ulong)address - s->vma, &path, &symbol, &line)) {
            if(!path || !symbol || !line) continue;
            return i({section(strz(path),'/',-2,-1), strz(symbol), (int)line});
        }
    }
    return i({string(),string(),0});
}
#else
#include "file.h"
#include "elf.h"
Symbol findNearestLine(void* address) {
    return i({string(),string(),0});
}
#endif
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
void logTrace() { logBacktrace(__builtin_frame_address(0)); }

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
    exit(-1);
}

struct action {
    void (*sigaction) (int, siginfo_t *, void *);
    uint mask[32];
    int flags = SA_SIGINFO;
    void (*restorer) (void) = 0;
};

static_this() {
#if BFD
    bfd_init();
    bfd = bfd_openr("/proc/self/exe",0);
    unsigned int size=0;
    long symcount = bfd_read_minisymbols(bfd, false, &syms, &size);
    if(symcount == 0) symcount = bfd_read_minisymbols(bfd, true, &syms, &size);
#else
    static Map map = mapFile("/proc/self/exe"_);
    auto elf = (Elf32_Ehdr*)map.data;
#endif
    action sa; sa.sigaction = &handler;
    for(int i=0;i<32;i++) sa.mask[i]=0;
    sigaction(SIGABRT, &sa, 0);
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGPIPE, &sa, 0);
    sigaction(SIGFPE, &sa, 0);
    //feenableexcept(FE_DIVBYZERO|FE_INVALID);
}
