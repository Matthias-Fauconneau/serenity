#include "trace.h"
#include "file.h"
#include "data.h"

struct Ehdr { byte ident[16]; uint16 type,machine; uint version; ptr entry,phoff,shoff; uint flags; uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint name,type; long flags,addr,offset,size; uint link,info; long addralign,entsize; };
#if __x86_64
struct Sym { uint	name; byte info,other; uint16 shndx; byte* value; long size; };
#else
struct Sym { uint	name; byte* value; uint size; byte info,other; uint16 shndx; };
#endif

/// Reads a little endian variable size integer
static int readLEV(BinaryData& s, bool sign=false) {
    int result=0; int shift=0; uint8 b;
    do { b = s.read(); result |= (b & 0x7f) << shift; shift += 7; } while(b & 0x80);
    if(sign && (shift < 32) && (b & 0x40)) result |= -1 << shift;
    return result;
}

string demangle(TextData& s, bool function=true) {
    string r;
    bool rvalue=false,ref=false; int pointer=0;
    for(;;) {
        /**/  if(s.match('O')) rvalue=true;
        else if(s.match('R')) ref=true;
        else if(s.match('K')) r<<"const "_;
        else if(function && s.match('L')) r<<"static "_;
        else if(s.match('P')) pointer++;
        else break;
    }
    uint l;
    /**/  if(s.match('v')) { if(pointer) r<<"void"_; }
    else if(s.match("C1"_)) r<< "this"_;
    else if(s.match("C2"_)) r<< "this"_;
    else if(s.match("D1"_)) r<< "~this"_;
    else if(s.match("D2"_)) r<< "~this"_;
    else if(s.match("Dv"_)){int size=s.integer(); s.match('_'); r<<demangle(s)+dec(size);}
    else if(s.match("eq"_)) r<<"operator =="_;
    else if(s.match("ix"_)) r<<"operator []"_;
    else if(s.match("cl"_)) r<<"operator ()"_;
    else if(s.match("ls"_)) r<<"operator <<"_;
    else if(s.match("rs"_)) r<<"operator >>"_;
    else if(s.match("cv"_)) r<<"operator "_ + demangle(s);
    else if(s.match("pl"_)) r<<"operator +"_;
    else if(s.match("mi"_)) r<<"operator -"_;
    else if(s.match("ml"_)) r<<"operator *"_;
    else if(s.match("dv"_)) r<<"operator /"_;
    else if(s.match('a')) r<<"byte"_;
    else if(s.match('b')) r<<"bool"_;
    else if(s.match('c')) r<<"char"_;
    else if(s.match('f')) r<<"float"_;
    else if(s.match('h')) r<<"byte"_;
    else if(s.match('i')) r<<"int"_;
    else if(s.match('j')) r<<"uint"_;
    else if(s.match('l')) r<<"long"_;
    else if(s.match('m')) r<<"ulong"_;
    else if(s.match('s')) r<<"short"_;
    else if(s.match('t')) r<<"ushort"_;
    else if(s.match('x')) r<<"int64"_;
    else if(s.match('y')) r<<"uint64"_;
    else if(s.match('A')) { r<<"[]"_; s.integer(); s.match('_'); }
    else if(s.match('M')) { r<<demangle(s)<<"::"_<<demangle(s); }
    else if(s.match("Tv"_)) { r<<"thunk "_; s.integer(); s.match('_'); if(s.match("n"_)) { s.integer(); s.match('_'); r<<demangle(s); } }
    else if(s.match('T')) { r<<'T'; s.integer(); s.match('_'); }
    else if(s.match("St"_)) r<<"std"_;
    else if(s.match('S')) { r<<'S'; s.integer(); s.match('_'); }
    else if(s.match('F')||s.match("Dp"_)) r << demangle(s);
    else if(s.match("Li"_)) r<<dec(s.integer());
    else if(s.match("Lj"_)) r<<dec(s.integer());
    else if(s.match("Lb"_)) r<<str((bool)s.integer());
    else if(s.match('L')) r<<"extern "_<<demangle(s);
    else if(s.match('I')||s.match('J')) { //template | argument pack
        array<string> args;
        while(s && !s.match('E')) {
            if(s.peek()=='Z') args<<(demangle(s)+"::"_+demangle(s));
            else args<<demangle(s,false);
        }
        r<<'<'<<join(args,", "_)<<'>';
    }
    else if(s.match('Z')) {
        r<< demangle(s);
        array<string> args;
        while(s && !s.match('E')) args << demangle(s);
        r<< '(' << join(args,", "_) << ')';
    }
    else if(s.match("_0"_)) {}
    else if(s.match('N')) {
        array<string> list;
        bool const_method =false;
        if(s.match('K')) const_method=true;
        while(s && !s.match('E')) {
            list<< demangle(s);
            if(s && (s.peek()=='I'||s.peek()=='J')) list.last()<< demangle(s);
        }
        r<< join(list,"::"_);
        if(const_method) r<< " const"_;
    } else {
        l=uint(s.integer());
        if(l<=s.available(l)) {
            r<<s.read(l); //struct
            if(s && s.peek()=='I') r<< demangle(s);
        } else r<<s.untilEnd();
    }
    for(int i=0;i<pointer;i++) r<<'*';
    if(rvalue) r<<"&&"_;
    if(ref) r<<'&';
    return r;
}
string demangle(const ref<byte>& symbol) { TextData s(symbol); return s.match('_')&&s.peek()=='Z'? demangle(s) : string(s.untilEnd()); }

Symbol findNearestLine(void* find) {
    static Map exe = "/proc/self/exe"_;
    const byte* elf = exe.data;
    const Ehdr& hdr = *(const Ehdr*)elf;
    ref<Shdr> sections = ref<Shdr>((const Shdr*)(elf+hdr.shoff),hdr.shnum);
    const char* shstrtab = elf+sections[hdr.shstrndx].offset;
    const char* strtab = 0; ref<Sym> symtab; BinaryData debug_line;
    for(const Shdr& s: sections)  {
        if(str(shstrtab+s.name)==".debug_line"_) debug_line=BinaryData(ref<byte>(elf+s.offset,s.size));
        else if(str(shstrtab+s.name)==".strtab"_) strtab=(const char*)elf+s.offset;
        else if(str(shstrtab+s.name)==".symtab"_) symtab=ref<Sym>((Sym*)(elf+s.offset),s.size/sizeof(Sym));
    }
    Symbol symbol;
    for(const Sym& sym: symtab) if(find >= sym.value && find < sym.value+sym.size) symbol.function = demangle(str(strtab+sym.name));
    for(BinaryData& s = debug_line;s.index<s.buffer.size();) {
        uint begin = s.index;
        struct CU { uint size; ushort version; uint prolog_size; uint8 min_inst_len, stmt; int8 line_base; uint8 line_range,opcode_base; } packed;
        const CU& cu = s.read<CU>();
        s.advance(cu.opcode_base-1);
        while(s.next()) s.untilNull();
        array<ref<byte>> files;
        while(s.peek()) {
            files << s.untilNull();
            int unused index = readLEV(s), unused time = readLEV(s), unused file_length=readLEV(s);
        }
        s.advance(1);
        byte* address = 0; uint file_index = 1, line = 1, is_stmt = cu.stmt;

        while(s.index<begin+cu.size+4) {
            uint8 opcode = s.read();
            enum { extended_op, op_copy, advance_pc, advance_line, set_file, set_column, negate_stmt, set_basic_block, const_add_pc,
                         fixed_advance_pc, set_prologue_end, set_epilogue_begin, set_isa };
            /**/ if(opcode >= cu.opcode_base) {
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
                /**/  if(opcode == end_sequence) { if (cu.stmt) { address = 0; file_index = 1; line = 1; is_stmt = cu.stmt; } }
                else if(opcode == set_address) { address = s.read<byte*>(); }
                else if(opcode == define_file) { readLEV(s); readLEV(s); }
                else if(opcode == set_discriminator) { readLEV(s); }
                else { warn("unknown opcode"_,opcode); s.advance(size); }
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
            else error("Unsupported"_,opcode);
        }
    }
    return symbol;
}

#if __x86_64 || __i386
void* caller_frame(void* fp) { return *(void**)fp; }
void* return_address(void* fp) { return *((void**)fp+1); }
#elif __arm__ // Assumes APCS stack layout (i.e works only when compiled with GCC's -mapcs flag)
void* caller_frame(void* fp) { return *((void**)fp-3); }
void* return_address(void* fp) { return *((void**)fp-1); }
#else
#error Unsupported architecture
#endif

string trace(int skip, void* ip) {
    void* stack[32]; clear((byte*)stack,sizeof(stack));
    void* frame = __builtin_frame_address(0);
    int i=0;
    for(;i<32;i++) {
#if __x86_64
        if(ptr(frame)<0x70000F000000 || ptr(frame)>0x800000000000) break; //1MB stack
#else
        if(ptr(frame)<0x1000) break;
#endif
        stack[i]=return_address(frame);
        frame=caller_frame(frame);
    }
    string r;
    for(i=i-3; i>=skip; i--) { Symbol s = findNearestLine(stack[i]); if(s.function||s.file||s.line) r<<(s.file+":"_+str(s.line)+"     \t"_+s.function+"\n"_); else r<<"0x"_+hex(ptr(stack[i]))<<"\n"_; }
    if(ip) { Symbol s = findNearestLine(ip); if(s.function||s.file||s.line) r<<(s.file+":"_+str(s.line)+"     \t"_+s.function+"\n"_); else r<<"0x"_+hex(ptr(ip))<<"\n"_; }
    return r;
}
