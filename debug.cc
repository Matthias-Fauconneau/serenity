#include "debug.h"
#include "file.h"
#include "stream.h"

struct Ehdr { byte ident[16]; uint16 type,machine; uint version,entry,phoff,shoff,flags;
              uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint	name,type,flags,addr,offset,size,link,info,addralign,entsize; };

/// Simple typed array reference (use /a array if you need memory management) //TODO: array : list ? array only? inline?
template<class T> struct list {
    const T* data; int size;
    list(const T* data, int size):data(data),size(size){}
    const T* begin() const  { return data; }
    const T* end() const { return data+size; }
    const T& operator[](int i) const { return data[i]; }
};

/// Reads a little endian variable size integer
static ulong readLEV(Stream& s, bool sign=false) {
    ulong result=0; int shift=0; byte b;
    do {
        b = s.next(); s.advance(1);
        result |= (b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);
    if (sign && (shift < 32) && (b & 0x40)) result |= -1 << shift;
    return result;
}

enum { extended_op, op_copy, advance_pc, advance_line, set_file, set_column, negate_stmt, set_basic_block, const_add_pc, fixed_advance_pc,
        set_prologue_end, set_epilogue_begin, set_isa };
enum { end_sequence = 1, set_address, define_file };

Symbol findNearestLine(void* find) {
    static Map map = mapFile("/proc/self/exe"_);
    const byte* elf = map.data;
    const Ehdr& hdr = *(Ehdr*)elf;
    auto sections = list<Shdr>((Shdr*)(elf+hdr.shoff),hdr.shnum);
    const char* strtab = (char*)elf+sections[hdr.shstrndx].offset;
    DataStream debug_line;
    for(const Shdr& s: sections)  if(str(strtab+s.name)==".debug_line"_) debug_line=array<byte>(elf+s.offset,s.size);
    for(DataStream& s = debug_line;s.index<s.buffer.size();) {
        int start = s.index;
        const struct { uint size; ushort version; uint prolog_size; ubyte min_inst_len, stmt; byte line_base; ubyte line_range,opcode_base; } packed&
                cu = s.read();
        s.advance(cu.opcode_base-1);
        while(s.next()) s.readString();
        s.advance(1);
        array<string> files;
        while(s.next()) {
            files << s.readString();
            int unused index = readLEV(s), unused time = readLEV(s), unused file_length=readLEV(s);
        }
        s.advance(1);
        byte* address = 0; uint file_index = 1, line = 1, is_stmt = cu.stmt;

        while(s.index<start+cu.size+4) {
            uint opcode = s.next(); s.advance(1);
            if (opcode >= cu.opcode_base) {
                opcode -= cu.opcode_base;
                line += (opcode % cu.line_range) + cu.line_base;
                int delta = (opcode / cu.line_range) * cu.min_inst_len;
                if(find>=address && find <= address+delta) {
                    return Symbol{ move(files[file_index-1]), ""_, line }; //TODO: function
                 }
                address += delta;
            }
            else if(opcode == extended_op) {
                int len = readLEV(s);
                if (len == 0) continue;
                opcode = s.next(); s.advance(1);
                if(opcode == end_sequence) {
                    if (cu.stmt) { address = 0; file_index = 1; line = 1; is_stmt = cu.stmt; }
                } else if(opcode == set_address) {
                    address = s.read<byte*>();
                } else error("Unsupported");
            } else if(opcode == op_copy) {}
            else if(opcode == advance_pc)  address += cu.min_inst_len * readLEV(s);
            else if(opcode == advance_line) line += readLEV(s);
            else if(opcode == set_file) file_index = readLEV(s);
            else if(opcode == set_column) readLEV(s);
            else if(opcode == negate_stmt) is_stmt = !is_stmt;
            else if(opcode == set_basic_block) {}
            else if(opcode == const_add_pc) address += ((255 - cu.opcode_base) / cu.line_range) * cu.min_inst_len;
            else if(opcode == fixed_advance_pc) address += s.read<ushort>();
            else if(opcode == set_prologue_end) {}
            else if(opcode == set_epilogue_begin) {}
            else if(opcode == set_isa) readLEV(s);
            else error("Unsupported",opcode);
        }
    }
    return {};
}

void logTrace() {
    {Symbol s = findNearestLine(__builtin_return_address(2));
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine(__builtin_return_address(1));
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
}

struct ucontext { ulong flags; ucontext *link; void* ss_sp; int ss_flags; size_t ss_size; ulong trap_no,error_code,oldmask;
    ulong arm_r0,arm_r1,arm_r2,arm_r3,arm_r4,arm_r5,arm_r6,arm_r7,arm_r8,arm_r9,arm_r10,arm_fp,arm_ip,arm_sp,arm_lr,arm_pc,arm_cpsr;
    ulong fault_address;
};
enum SW { IE = 1, DE = 2, ZE = 4, OE = 8, UE = 16, PE = 32 };
enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM	};

static void handler(int sig, struct siginfo*, ucontext* context) {
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
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);
#elif __i386__
    logBacktrace(context->uc_mcontext.gregs[REG_EBP]);
    Symbol s = findNearestLine((void*)context->uc_mcontext.gregs[REG_EIP]);
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);
#elif __arm__
    //logBacktrace((void*)context->arm_fp);
    /*{Symbol s = findNearestLine((void*)context->arm_lr);
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);}*/
    {Symbol s = findNearestLine((void*)context->arm_pc);
    log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
#else
#error Unsupported architecture
#endif
    exit(-1);
}

enum { SA_SIGINFO=4 };
struct action {
    void (*sigaction) (int, struct siginfo*, ucontext*);
    uint mask[2];
    ulong flags = SA_SIGINFO;
    void (*restorer) (void) = 0;
};

void catchErrors() {
    action sa; sa.sigaction = &handler;
    sigaction(SIGABRT, &sa, 0, 8);
    sigaction(SIGSEGV, &sa, 0, 8);
    sigaction(SIGTERM, &sa, 0, 8);
    sigaction(SIGPIPE, &sa, 0, 8);
    sigaction(SIGFPE, &sa, 0, 8);
    //feenableexcept(FE_DIVBYZERO|FE_INVALID);
}
