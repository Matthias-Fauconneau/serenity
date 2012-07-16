#include "debug.h"
#include "file.h"
#include "stream.h"
#include "linux.h"

const char* errno[35] = {"OK",
    "PERM","NOENT","SRCH","INTR","IO","NXIO","2BIG","NOEXEC","BADF","CHILD","AGAIN","NOMEM","ACCES","FAULT","NOTBLK","BUSY","EXIST",
    "XDEV","NODEV","NOTDIR","ISDIR","INVAL","NFILE","MFILE","NOTTY","TXTBSY","FBIG","NOSPC","SPIPE","ROFS","MLINK","PIPE","DOM","RANGE"};

void write(int fd, const array<byte>& s) { int r=write(fd,s.data(),s.size()); assert(r==(int)s.size(),r); }
void abort() { exit(-1); }
void log_(const char* expr) { log(expr); }

struct Ehdr { byte ident[16]; uint16 type,machine; uint version,entry,phoff,shoff,flags;
              uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint name,type,flags,addr,offset,size,link,info,addralign,entsize; };
struct Sym { uint	name; byte* value; uint size; ubyte info,other; uint16 shndx; };

/// Reads a little endian variable size integer
static int readLEV(DataStream& s, bool sign=false) {
    int result=0; int shift=0; byte b;
    do { b = s.read(); result |= (b & 0x7f) << shift; shift += 7; } while(b & 0x80);
    if(sign && (shift < 32) && (b & 0x40)) result |= -1 << shift;
    return result;
}

string demangle(TextStream& s) {
    string r;
    bool rvalue=false,ref=false; int pointer=0;
    for(;;) {
        if(s.match('O')) rvalue=true;
        else if(s.match('R')) ref=true;
        else if(s.match('K')) r<<"const "_;
        else if(s.match('L')) r<<"static "_;
        else if(s.match('P')) pointer++;
        else break;
    }
    int l;
    if(s.match('v')) { if(pointer) r<<"void"_; }
    else if(s.match('b')) r<<"bool"_;
    else if(s.match('c')) r<<"char"_;
    else if(s.match('a')) r<<"byte"_;
    else if(s.match('h')) r<<"ubyte"_;
    else if(s.match('s')) r<<"short"_;
    else if(s.match('t')) r<<"ushort"_;
    else if(s.match('i')) r<<"int"_;
    else if(s.match('j')) r<<"uint"_;
    else if(s.match('l')) r<<"long"_;
    else if(s.match('m')) r<<"ulong"_;
    else if(s.match('x')) r<<"int64"_;
    else if(s.match('y')) r<<"uint64"_;
    else if(s.match("T_"_)) r<<'T';
    else if(s.match('S')) { r<<'S'; s.number(); s.match('_'); }
    else if(s.match('F')||s.match("Dp"_)) r << demangle(s);
    else if(s.match('I')||s.match('J')) { //template | argument pack
        array<string> args;
        while(s && !s.match('E')) {
            if(s.peek()=='Z') args<<(demangle(s)+"::"_+demangle(s));
            else args<<demangle(s);
        }
        r<<'<'<<join(args,", "_)<<'>';
    } else if(s.match('Z')) {
        bool const_method =false;
        if(s.match('N')) {
            array<string> list;
            if(s.match('K')) const_method=true;
            while(s && !s.match('E')) {
                if(s.match("C1"_)) list << copy(list.first()); //constructor
                else if(s.match("C2"_)) list << copy(list.first()); //constructor
                else if(s.match("ix"_)) list << string("operator []"_);
                else if(s.match("cl"_)) list << string("operator ()"_);
                else if(s.match("ls"_)) list << string("operator <<"_);
                else if(s.match("cv"_)) list << string("operator "_ + demangle(s));
                else if((l=s.number())!=-1) {
                    list << string(s.read(l)); //class/member
                    if(s.peek()=='I') list.last()<< demangle(s);
                } else if(s.peek()=='I') list.last()<< demangle(s);
                else error('N',r,string(s.untilEnd()),string(move(s.buffer)));
            }
            r<< join(list,"::"_);
        } else if((l=s.number())!=-1) {
            r<< s.read(l); //function
            if(s.peek()=='I') r<< demangle(s);
        }
        array<string> args;
        while(s && !s.match('E')) args << demangle(s);
        r<< '(' << join(args,", "_) << ')';
        if(const_method) r<< " const"_;
    } else if(s.match("_0"_)) {
    } else if((l=s.number())!=-1) {
        r<<s.read(l); //struct
        if(s && s.peek()=='I') r<< demangle(s);
    } else { log(s.slice(s.index,s.buffer.size()-s.index)); return string(s.untilEnd()); }
    for(int i=0;i<pointer;i++) r<<'*';
    if(rvalue) r<<"&&"_;
    if(ref) r<<'&';
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
            symbol.function = s.match('_')&&s.peek()=='Z'? (s.buffer.size()>80?string("delegate"_) :demangle(s)) : string(s.untilEnd());
        }
    }
    for(DataStream& s = debug_line;s.index<s.buffer.size();) {
        int start = s.index;
        struct CU { uint size; ushort version; uint prolog_size; ubyte min_inst_len, stmt; int8 line_base; ubyte line_range,opcode_base; } packed;
        const CU& cu = s.read();
        s.advance(cu.opcode_base-1);
        while(s.peek()) s.untilNull();
        s.advance(1);
        array<string> files;
        while(s.peek()) {
            files << s.untilNull();
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
                line += (opcode % cu.line_range) + cu.line_base;
                if(find>=address && find<address+delta) { symbol.file=move(files[file_index-1]); symbol.line=line; return symbol; }
                address += delta;
            }
            else if(opcode == extended_op) {
                int size = readLEV(s);
                if (size == 0) continue;
                opcode = s.read();
                enum { end_sequence = 1, set_address, define_file, set_discriminator };
                /***/ if(opcode == end_sequence) { if (cu.stmt) { address = 0; file_index = 1; line = 1; is_stmt = cu.stmt; } }
                else if(opcode == set_address) { address = s.read<byte*>(); }
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

void trace(int skip) {
    static bool recurse; if(recurse) {log("Debugger error");return;} recurse=true;
    void* stack[8] = {0,0,0,0,0,0,0,0};
    stack[0] = __builtin_return_address(0);
#define bra(i) if(stack[i-1]) stack[i] = __builtin_return_address(i);
    bra(1) bra(2) bra(3) bra(4) bra(5) bra(6) bra(7)
            for(int i=7;i>=skip;i--) if(stack[i]) { Symbol s = findNearestLine(stack[i]); log(s.file+":"_+str(s.line)+"    \t"_+s.function); }
    recurse=false;
}

enum { SIGABRT=6, SIGIOT, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM };

static void handler(int sig, struct siginfo*, struct ucontext*) {
    if(sig == SIGSEGV) log("Segmentation violation"_);
    trace(1);
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
