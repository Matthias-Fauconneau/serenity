#include "trace.h"
#include "file.h"
#include "data.h"
#include <unistd.h>

struct Ehdr { byte ident[16]; uint16 type,machine; uint version; ptr entry,phoff,shoff; uint flags; uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint name,type; long flags,addr,offset,size; uint link,info; long addralign,entsize; };
struct Sym { uint	name; byte info,other; uint16 shndx; byte* value; long size; };

/// Reads a little endian variable size integer
static int readLEV(BinaryData& s, bool sign=false) {
    int result=0; int shift=0; uint8 b;
    do { b = s.read(); result |= (b & 0x7f) << shift; shift += 7; } while(b & 0x80);
    if(sign && (shift < 32) && (b & 0x40)) result |= -1 << shift;
    return result;
}

String demangle(TextData& s, bool function=true) {
    String r;
    bool rvalue=false,ref=false; int pointer=0;
    for(;;) {
        /**/  if(s.match('O')) rvalue=true;
        else if(s.match('R')) ref=true;
        else if(s.match('K')) r.append("const "_);
        else if(function && s.match('L')) r.append("static "_);
        else if(s.match('P')) pointer++;
        else break;
    }
    uint l;
    /**/  if(s.match('v')) { if(pointer) r.append("void"_); }
    else if(s.match("C1"_)) r.append( "this"_);
    else if(s.match("C2"_)) r.append( "this"_);
    else if(s.match("D1"_)) r.append( "~this"_);
    else if(s.match("D2"_)) r.append( "~this"_);
    else if(s.match("Dv"_)){int size=s.integer(); s.match('_'); r.append(demangle(s)+dec(size));}
    else if(s.match("eq"_)) r.append("operator =="_);
    else if(s.match("ix"_)) r.append("operator []"_);
    else if(s.match("cl"_)) r.append("operator ()"_);
    else if(s.match("ls"_)) r.append("operator <<"_);
    else if(s.match("rs"_)) r.append("operator >>"_);
    else if(s.match("cv"_)) r.append("operator "_ + demangle(s));
    else if(s.match("pl"_)) r.append("operator +"_);
    else if(s.match("mi"_)) r.append("operator -"_);
    else if(s.match("ml"_)) r.append("operator *"_);
    else if(s.match("dv"_)) r.append("operator /"_);
    else if(s.match('a')) r.append("byte"_);
    else if(s.match('b')) r.append("bool"_);
    else if(s.match('c')) r.append("char"_);
    else if(s.match('f')) r.append("float"_);
    else if(s.match('h')) r.append("byte"_);
    else if(s.match('i')) r.append("int"_);
    else if(s.match('j')) r.append("uint"_);
    else if(s.match('l')) r.append("long"_);
    else if(s.match('m')) r.append("ulong"_);
    else if(s.match('s')) r.append("short"_);
    else if(s.match('t')) r.append("ushort"_);
    else if(s.match('x')) r.append("int64"_);
    else if(s.match('y')) r.append("uint64"_);
    else if(s.match('A')) { r.append("[]"_); s.whileInteger(); s.match('_'); }
    else if(s.match('M')) { r.append(demangle(s)); r.append("::"_); r.append(demangle(s)); }
    else if(s.match("Tv"_)) { r.append("thunk "_); s.whileInteger(); s.match('_'); if(s.match("n"_)) { s.whileInteger(); s.match('_'); r.append(demangle(s)); } }
    else if(s.match('T')) { r.append('T'); s.whileInteger(); s.match('_'); }
    else if(s.match("St"_)) r.append("std"_);
    else if(s.match('S')) { r.append('S'); s.whileInteger(); s.match('_'); }
    else if(s.match('F')||s.match("Dp"_)) r.append(demangle(s));
    else if(s.match("Li"_)) r.append(dec(s.integer()));
    else if(s.match("Lj"_)) r.append(dec(s.integer()));
    else if(s.match("Lb"_)) r.append(str((bool)s.integer()));
    else if(s.match('L')) { r.append("extern "_); r.append(demangle(s)); }
    else if(s.match('I')||s.match('J')) { //template | argument pack
        array<String> args;
        while(s && !s.match('E')) {
            if(s.wouldMatch('Z')) args.append(demangle(s)+"::"_+demangle(s));
            else args.append(demangle(s,false));
        }
        r.append('<'); r.append(join(args,", "_)); r.append('>');
    }
    else if(s.match('Z')) {
        r.append( demangle(s));
        array<String> args;
        while(s && !s.match('E')) args.append(demangle(s));
        r.append('('); r.append(join(args,", "_)); r.append(')');
    }
    else if(s.match("_0"_)) {}
    else if(s.match('N')) {
        array<String> list;
        bool const_method =false;
        if(s.match('K')) const_method=true;
        while(s && !s.match('E')) {
            list.append( demangle(s) );
            if(s.wouldMatchAny("IJ"_)) list.last().append( demangle(s) );
        }
        r.append( join(list,"::"_) );
        if(const_method) r.append(" const"_);
    } else {
        l=s.mayInteger(-1);
        if(l<=s.available(l)) {
            r.append(s.read(l)); //struct
            if(s.wouldMatch('I')) r.append(demangle(s));
        } else r.append(s.untilEnd());
    }
    for(int i=0;i<pointer;i++) r.append('*');
    if(rvalue) r.append("&&"_);
    if(ref) r.append('&');
    return r;
}
String demangle(const string symbol) { TextData s(symbol); s.match('_'); return demangle(s); }

Symbol findSymbol(void* find) {
    static Map exe("/proc/self/exe"_);
    const byte* elf = exe.data;
    const Ehdr& hdr = *(const Ehdr*)elf;
    ref<Shdr> sections = ref<Shdr>((const Shdr*)(elf+hdr.shoff),hdr.shnum);
    const char* shstrtab = elf+sections[(uint)hdr.shstrndx].offset;
    const char* strtab = 0; ref<Sym> symtab; BinaryData debug_line;
    for(const Shdr& s: sections)  {
        if(str(shstrtab+s.name)==".debug_line"_) new (&debug_line) BinaryData(ref<byte>(elf+s.offset,s.size));
        else if(str(shstrtab+s.name)==".strtab"_) strtab=(const char*)elf+s.offset;
        else if(str(shstrtab+s.name)==".symtab"_) symtab=ref<Sym>((Sym*)(elf+s.offset),s.size/sizeof(Sym));
    }
    Symbol symbol;
    for(const Sym& sym: symtab) if(find >= sym.value && find < sym.value+sym.size) { symbol.function = demangle(str(strtab+sym.name)); break; }
    for(BinaryData& s = debug_line;s.index<s.buffer.size;) {
        uint begin = s.index;
        struct CU { uint size; uint16 version; uint prolog_size; uint8 min_inst_len, stmt; int8 line_base; uint8 line_range,opcode_base; } packed;
        const CU& cu = s.read<CU>();
        s.advance(cu.opcode_base-1);
        while(s.next()) { s.whileNot(0); s.skip('\0'); }
        array<string> files;
        while(s.peek()) {
            files.append( cast<char>(s.whileNot(0)) ); s.skip('\0');
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
                else { error("unknown opcode"_,opcode); s.advance(size); }
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
                uint16 delta = s.read();
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

void* caller_frame(void* fp) { return *(void**)fp; }
void* return_address(void* fp) { return *((void**)fp+1); }

String trace(int skip, void* ip) {
    String log;
    void* stack[32];
    int i=0;
    void* frame = __builtin_frame_address(0);
    for(;i<32;i++) {
        if(ptr(frame)<0x70000F000000 || ptr(frame)>0x800000000000) break; //1MB stack
        stack[i]=return_address(frame);
        frame=caller_frame(frame);
    }
    for(i=i-4; i>=skip; i--) {
        Symbol s = findSymbol(stack[i]);
        if(s.function||s.file||s.line) log.append(left(s.file+":"_+str(s.line),16)+" "_+s.function+"\n"_);
        else log.append("0x"_+hex(ptr(stack[i]))+"\n"_);
    }
    if(ip) {
        Symbol s = findSymbol(ip);
        if(s.function||s.file||s.line) log.append(left(s.file+":"_+str(s.line),16)+" "_+s.function+"\n"_);
        else log.append("0x"_+hex(ptr(ip))+"\n"_);
    }
    return log;
}
