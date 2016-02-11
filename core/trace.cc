#include "trace.h"
#include "file.h"
#include "data.h"
#include <unistd.h>

struct Ehdr { byte ident[16]; uint16 type,machine; uint version; ptr entry,phoff,shoff; uint flags; uint16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; };
struct Shdr { uint name,type; long flags,addr,offset,size; uint link,info; long addralign,entsize; };
struct Sym { uint name; byte info,other; uint16 shndx; byte* value; long size; };

/// Reads a little endian variable size integer
static int readLEV(BinaryData& s, bool sign=false) {
    uint result=0; uint shift=0; uint8 b;
    do { b = s.read(); result |= (b & 0x7f) << shift; shift += 7; } while(b & 0x80);
    if(sign && (shift < 32) && (b & 0x40)) result |= uint(~0) << shift;
    return result;
}

String demangle(TextData& s, bool function=true) {
	array<char> r;
    bool rvalue=false,ref=false; int pointer=0;
    for(;;) {
        /**/  if(s.match('O')) rvalue=true;
        else if(s.match('R')) ref=true;
        else if(s.match('K')) r.append("const ");
        else if(function && s.match('L')) r.append("static ");
        else if(s.match('P')) pointer++;
        else break;
    }
    uint l;
    /**/  if(s.match('v')) { if(pointer) r.append("void"); }
    else if(s.match("C1")) r.append( "this");
    else if(s.match("C2")) r.append( "this");
    else if(s.match("D1")) r.append( "~this");
    else if(s.match("D2")) r.append( "~this");
    else if(s.match("Dv")){int size=s.integer(); s.match('_'); r.append(demangle(s)+str(size));}
    else if(s.match("eq")) r.append("operator==");
    else if(s.match("ix")) r.append("operator[]");
    else if(s.match("cl")) r.append("operator()");
    else if(s.match("ls")) r.append("operator<<");
    else if(s.match("rs")) r.append("operator>>");
    else if(s.match("cv")) r.append("operator" + demangle(s));
    else if(s.match("pl")) r.append("operator+");
    else if(s.match("mi")) r.append("operator-");
    else if(s.match("ml")) r.append("operator*");
    else if(s.match("dv")) r.append("operator/");
    else if(s.match('a')) r.append("byte");
    else if(s.match('b')) r.append("bool");
    else if(s.match('c')) r.append("char");
	else if(s.match('d')) r.append("double");
    else if(s.match('f')) r.append("float");
    else if(s.match('h')) r.append("byte");
    else if(s.match('i')) r.append("int");
    else if(s.match('j')) r.append("uint");
    else if(s.match('l')) r.append("long");
    else if(s.match('m')) r.append("ulong");
    else if(s.match('s')) r.append("short");
    else if(s.match('t')) r.append("ushort");
    else if(s.match('x')) r.append("int64");
    else if(s.match('y')) r.append("uint64");
    else if(s.match('A')) { r.append("[]"); s.whileInteger(); s.match('_'); }
    else if(s.match('M')) { r.append(demangle(s)); r.append("::"); r.append(demangle(s)); }
    else if(s.match("Tv")) { r.append("thunk "); s.whileInteger(); s.match('_'); if(s.match("n")) { s.whileInteger(); s.match('_'); r.append(demangle(s)); } }
    else if(s.match('T')) { r.append('T'); s.whileInteger(); s.match('_'); }
    else if(s.match("St")) r.append("std");
    else if(s.match('S')) { r.append('S'); s.whileInteger(); s.match('_'); }
    else if(s.match('F')||s.match("Dp")) r.append(demangle(s));
    else if(s.match("Li")) r.append(str(s.integer()));
    else if(s.match("Lj")) r.append(str(s.integer()));
    //else if(s.match("Lb")) r.append(str((bool)s.integer()));
    else if(s.match('L')) { r.append("extern "); r.append(demangle(s)); }
    else if(s.match('I')||s.match('J')) { //template | argument pack
        r.append('<');
        while(s && !s.match('E')) {
            if(s.wouldMatch('Z')) r.append(demangle(s)+"::"+demangle(s));
            else r.append(demangle(s,false));
            r.append(", ");
        }
        if(r.size>=3) r.shrink(r.size-2); r.append('>');
    }
    else if(s.match('Z')) {
        r.append(demangle(s));
        r.append('(');
        while(s && !s.match('E')) { r.append(demangle(s)); r.append(", "); }
        if(r.size>=3) r.shrink(r.size-2); r.append(')');
    }
    else if(s.match("_0")) {}
    else if(s.match('N')) {
        bool const_method =false;
        if(s.match('K')) const_method=true;
        while(s && !s.match('E')) {
            r.append( demangle(s) );
            if(s.wouldMatchAny("IJ")) r.append( demangle(s) );
            r.append("::");
        }
        if(r.size>=3) r.shrink(r.size-2);
        if(const_method) r.append(" const");
    } else {
        l = s.mayInteger(-1);
        if(l<=s.available(l)) {
            r.append(s.read(l)); //struct
            if(s.wouldMatch('I')) r.append(demangle(s));
        } else if(s) r.append(s.untilEnd());
    }
    for(int i=0;i<pointer;i++) r.append('*');
    if(rvalue) r.append("&&");
    if(ref) r.append('&');
	return move(r);
}
String demangle(const string symbol) { TextData s(symbol); s.match('_'); return demangle(s); }

Symbol findSymbol(void* find) {
    static Map exe("/proc/self/exe");
    const byte* elf = exe.data;
    const Ehdr& hdr = *(const Ehdr*)elf;
    ref<Shdr> sections = ref<Shdr>((const Shdr*)(elf+hdr.shoff),hdr.shnum);
    const char* shstrtab = elf+sections[(uint)hdr.shstrndx].offset;
    const char* strtab = 0; ref<Sym> symtab; BinaryData debug_line;
    for(const Shdr& s: sections)  {
        string name = str(shstrtab+s.name);
        /**/  if(name==".debug_line") new (&debug_line) BinaryData(ref<byte>(elf+s.offset,s.size));
        else if(name==".strtab") strtab=(const char*)elf+s.offset;
        else if(name==".symtab") symtab=ref<Sym>((Sym*)(elf+s.offset),s.size/sizeof(Sym));
    }
    Symbol symbol;
    for(const Sym& sym: symtab) {
        if(find >= sym.value && find < sym.value+sym.size) {
         symbol.function = demangle(str(strtab+sym.name));
         break;
        }
    }
    for(BinaryData& s = debug_line; s;) {
        uint begin = s.index;
        struct CU { uint size; uint16 version; uint prolog_size; uint8 min_inst_len, stmt; int8 line_base; uint8 line_range,opcode_base; } packed;
        const CU& cu = s.read<CU>();
        s.advance(cu.opcode_base-1);
        while(s.next()) { s.whileNot(0); s.skip('\0'); }
		/*Array*/array<string/*, 1024*/> files;
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
                if(find>=address && find<address+delta) {
                    symbol.file = files[file_index-1];
                    symbol.line=line;
                    return ::move(symbol);
                }
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
                else error("Unknown opcode");
            }
            else if(opcode == op_copy) {}
            else if(opcode == advance_pc) {
                int delta = cu.min_inst_len * readLEV(s);
                if(find>=address && find<address+delta) { symbol.file=files[file_index-1]; symbol.line=line; return symbol; }
                address += delta;
            }
            else if(opcode == advance_line) line += readLEV(s,true);
            else if(opcode == set_file) file_index = readLEV(s);
            else if(opcode == set_column) readLEV(s);
            else if(opcode == negate_stmt) is_stmt = !is_stmt;
            else if(opcode == set_basic_block) {}
            else if(opcode == const_add_pc) {
                uint delta = ((255u - cu.opcode_base) / cu.line_range) * cu.min_inst_len;
                if(find>=address && find<address+delta) { symbol.file=files[file_index-1]; symbol.line=line; return symbol; }
                address += delta;
            }
            else if(opcode == fixed_advance_pc) {
                uint16 delta = s.read();
                if(find>=address && find<address+delta) { symbol.file=files[file_index-1]; symbol.line=line; return symbol; }
                 address += delta;
            }
            else if(opcode == set_prologue_end) {}
            else if(opcode == set_epilogue_begin) {}
            else if(opcode == set_isa) readLEV(s);
            else error("Unknown opcode");
        }
    }
    return symbol;
}

void* caller_frame(void* fp) { return *(void**)fp; }
void* return_address(void* fp) { return *((void**)fp+1); }
#include <execinfo.h>

String trace(int skip, void* ip) {
    array<char> log;
    void* stack[32];
    int i = backtrace(stack, 32);
    for(i=i-4; i>=skip; i--) {
        Symbol s = findSymbol(stack[i]);
        if(s.function||s.file||s.line) log.append(left(s.file+':'+str(s.line),16)+'\t'+s.function+'\n');
        else log.append("0x"+hex(ptr(stack[i]))+'\n');
    }
    if(ip) {
        Symbol s = findSymbol(ip);
        if(s.function||s.file||s.line) log.append(left(s.file+':'+str(s.line),16)+'\t'+s.function+'\n');
        else log.append("0x"+hex(ptr(ip))+'\n');
    }
    log.pop(); // Pops last \n
    return move(log);
}

void logTrace() { log(trace()); }
