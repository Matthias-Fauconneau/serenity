#include "debug.h"
#include "file.h"
#include "stream.h"
#include "linux.h"

void write(int fd, const array<byte>& s) { write(fd,s.data(),(ulong)s.size()); }
void abort() { exit(-1); }

struct Ehdr { byte ident[16]; uint16 type,machine; uint version,entry,phoff,shoff,flags;
              uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint name,type,flags,addr,offset,size,link,info,addralign,entsize; };
struct Sym { uint	name; byte* value; uint size; ubyte info,other; uint16 shndx; };

/// Reads a little endian variable size integer
static int readLEV(Stream& s, bool sign=false) {
    int result=0; int shift=0; byte b;
    do { b = s.next(); s.advance(1); result |= (b & 0x7f) << shift; shift += 7; } while(b & 0x80);
    if(sign && (shift < 32) && (b & 0x40)) result |= -1 << shift;
    return result;
}

string demangle(TextStream& s) {
    string r;
    bool rvalue=false,ref=false; int pointer=0;
    for(;;) {
        if(s.match("O"_)) rvalue=true;
        else if(s.match("R"_)) ref=true;
        else if(s.match("K"_)) r<<"const "_;
        else if(s.match("L"_)) r<<"static "_;
        else if(s.match("P"_)) pointer++;
        else break;
    }
    int l;
    if(s.match("v"_)) { if(pointer) r<<"void"_; }
    else if(s.match("b"_)) r<<"bool"_;
    else if(s.match("c"_)) r<<"char"_;
    else if(s.match("a"_)) r<<"byte"_;
    else if(s.match("h"_)) r<<"ubyte"_;
    else if(s.match("s"_)) r<<"short"_;
    else if(s.match("t"_)) r<<"ushort"_;
    else if(s.match("i"_)) r<<"int"_;
    else if(s.match("j"_)) r<<"uint"_;
    else if(s.match("l"_)) r<<"long"_;
    else if(s.match("m"_)) r<<"ulong"_;
    else if(s.match("x"_)) r<<"int64"_;
    else if(s.match("y"_)) r<<"uint64"_;
    else if(s.match("T_"_)) r<<"T"_;
    else if(s.match("S"_)) { r<<"S"_; s.number(); s.match("_"_); }
    else if(s.match("I"_)) { //template
        array<string> args;
        while(s && !s.match("E"_)) {
            if(s.get(1)=="Z"_) args<<(demangle(s)+"::"_+demangle(s));
            else args<<demangle(s);
        }
        r<<"<"_<<join(args,", "_)<<">"_;
    } else if(s.match("Z"_)) {
        bool const_method =false;
        if(s.match("N"_)) {
            array<string> list;
            if(s.match("K"_)) const_method=true;
            while(s && !s.match("E"_)) {
                if(s.match("C1"_)) list << list.first(); //constructor
                else if(s.match("C2"_)) list << list.first(); //constructor
                else if(s.match("ix"_)) list << "operator []"_;
                else if(s.match("cl"_)) list << "operator ()"_;
                else if(s.match("cv"_)) list << ("operator "_ + demangle(s));
                else if((l=s.number())!=-1) {
                    list << s.read(l); //class/member
                    if(s.next()=='I') list.last()<< demangle(s);
                } else if(s.next()=='I') list.last()<< demangle(s);
                else error("N"_,r,string(s.readAll()),string(move(s.buffer)));
            }
            r<< join(list,"::"_);
        } else if((l=s.number())!=-1) {
            r<< s.read(l); //function
            if(s.next()=='I') r<< demangle(s);
        }
        array<string> args;
        while(s && !s.match("E"_)) args << demangle(s);
        r<< "("_ << join(args,", "_) << ")"_;
        if(const_method) r<< " const"_;
    }
    else if(s.match("_0"_)) {}
    else if((l=s.number())!=-1) {
        r<<s.read(l); //struct
        if(s && s.next()=='I') r<< demangle(s);
    }
    else error("A"_,r,string(s.readAll()));
    for(int i=0;i<pointer;i++) r<<"*"_;
    if(rvalue) r<<"&&"_;
    if(ref) r<<"&"_;
    return r;
}

Symbol findNearestLine(void* find) {
    static Map map = mapFile("proc/self/exe"_);
    const byte* elf = map.data;
    const Ehdr& hdr = *(Ehdr*)elf;
    array<Shdr> sections = array<Shdr>((Shdr*)(elf+hdr.shoff),hdr.shnum);
    const char* shstrtab = (char*)elf+sections[hdr.shstrndx].offset;
    const char* strtab = 0; DataStream symtab; DataStream debug_line;
    for(const Shdr& s: sections)  {
        if(str(shstrtab+s.name)==".debug_line"_) debug_line=array<byte>(elf+s.offset,s.size);
        else if(str(shstrtab+s.name)==".symtab"_) symtab=array<byte>(elf+s.offset,s.size);
        else if(str(shstrtab+s.name)==".strtab"_) strtab=(const char*)elf+s.offset;
    }
    Symbol symbol;
    for(DataStream& s = symtab;s.index<s.buffer.size();) {
        const Sym& sym = s.read();
        if(find >= sym.value && find < sym.value+sym.size) {
            TextStream s(str(strtab+sym.name));
            symbol.function = s.match("_"_)&&s.next()=='Z'? demangle(s) : s.readAll();
        }
    }
    for(DataStream& s = debug_line;s.index<s.buffer.size();) {
        int start = s.index;
        struct CU { uint size; ushort version; uint prolog_size; ubyte min_inst_len, stmt; int8 line_base; ubyte line_range,opcode_base; } packed;
        const CU& cu = s.read();
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
            ubyte opcode = s.read();
            enum { extended_op, op_copy, advance_pc, advance_line, set_file, set_column, negate_stmt, set_basic_block, const_add_pc,
                         fixed_advance_pc, set_prologue_end, set_epilogue_begin, set_isa };
            /***/ if (opcode >= cu.opcode_base) {
                opcode -= cu.opcode_base;
                int delta = (opcode / cu.line_range) * cu.min_inst_len;
                if(find>=address && find<address+delta) { symbol.file=move(files[file_index-1]); symbol.line=line; return symbol; }
                line += (opcode % cu.line_range) + cu.line_base;
                address += delta;
            }
            else if(opcode == extended_op) {
                int size = readLEV(s);
                if (size == 0) continue;
                opcode = s.read();
                enum { end_sequence = 1, set_address, define_file, set_discriminator };
                /***/ if(opcode == end_sequence) { if (cu.stmt) { address = 0; file_index = 1; line = 1; is_stmt = cu.stmt; } }
                else if(opcode == set_address) { address = s.read(); }
                else if(opcode == define_file) { readLEV(s); readLEV(s); }
                else if(opcode == set_discriminator) { readLEV(s); }
                else { log("UNKNOWN: length",size); s.advance(size); }
            }
            else if(opcode == op_copy) {}
            else if(opcode == advance_pc) {
                int delta = cu.min_inst_len * readLEV(s);
                if(find>=address && find<address+delta) { symbol.file=move(files[file_index-1]); symbol.line=line; return symbol; }
                address += delta;
            }
            else if(opcode == advance_line) line += readLEV(s,true);
            else if(opcode == set_file) file_index = readLEV(s);
            else if(opcode == set_column) readLEV(s);
            else if(opcode == negate_stmt) is_stmt = !is_stmt;
            else if(opcode == set_basic_block) {}
            else if(opcode == const_add_pc) {
                uint delta = ((255u - cu.opcode_base) / cu.line_range) * cu.min_inst_len;
                if(find>=address && find<address+delta) { symbol.file=move(files[file_index-1]); symbol.line=line; return symbol; }
                address += delta;
            }
            else if(opcode == fixed_advance_pc) {
                ushort delta = s.read();
                if(find>=address && find<address+delta) { symbol.file=move(files[file_index-1]); symbol.line=line; return symbol; }
                 address += delta;
            }
            else if(opcode == set_prologue_end) {}
            else if(opcode == set_epilogue_begin) {}
            else if(opcode == set_isa) readLEV(s);
            else error("Unsupported",opcode);
        }
    }
    return symbol;
}

void trace() {
    static bool recurse; if(recurse) {log("Debugger error");return;} recurse=true;
    {Symbol s = findNearestLine(__builtin_return_address(4)); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine(__builtin_return_address(3)); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine(__builtin_return_address(2)); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine(__builtin_return_address(1)); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine(__builtin_return_address(0)); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    recurse=false;
}

struct ucontext {
    ulong flags; ucontext *link; void* ss_sp; int ss_flags; size_t ss_size;
#if __arm__
    ulong trap,err,mask,r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,fp,ip,sp,lr,pc,cpsr,fault;
#elif __x86_64__ || __i386__
    ulong gs,fs,es,ds,edi,esi,ebp,esp,ebx,edx,ecx,eax,trap,err,eip,cs,efl,uesp,ss;
#endif
};
enum SW { IE = 1, DE = 2, ZE = 4, OE = 8, UE = 16, PE = 32 };
enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM	};

static void handler(int sig, struct siginfo*, ucontext* context) {
    if(sig == SIGSEGV) log("Segmentation violation"_);
#if __arm__
    {Symbol s = findNearestLine((void*)context->lr); log(s.file+":"_+str(s.line)+"   \t"_+s.function);}
    {Symbol s = findNearestLine((void*)context->pc); log(s.file+":"_+str(s.line)+"   \t"_+s.function); }
#elif __x86_64__ || __i386__
    {Symbol s = findNearestLine(*(*((void***)context->ebp)+1)); log(s.file+":"_+str(s.line)+"  \t"_+s.function);}
    {Symbol s = findNearestLine(*((void**)context->ebp+1)); log(s.file+":"_+str(s.line)+"  \t"_+s.function);}
    {Symbol s = findNearestLine((void*)context->eip); log(s.file+":"_+str(s.line)+"  \t"_+s.function);}
#endif
    abort();
}

void catchErrors() {
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
}
