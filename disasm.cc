#include "disasm.h"
#include "string.h"
#include "debug.h"

int imm(const byte*& c, int size) {
    int imm;
    /**/  if(size==0) imm=*(int8*)c, c++;
    else if(size==1) imm=*(int16*)c, c+=2;
    else if(size==2) imm=*(int32*)c, c+=4;
    else if(size==3) imm=*(int64*)c, c+=8;
    else error("Unknown size");
    return imm;
}

enum Reg { rAX,rCX,rDX,rBX,rSP,rBP,rSI,rDI,r8,r9,r10,r11,r12,r13,r14,r15 };
string gpr(uint r, uint size) {
    assert(r<16);
    static constexpr ref<byte> gprs[] = {"ax"_,"cx"_,"dx"_,"bx"_,"sp"_,"bp"_,"si"_,"di"_,"r8"_,"r9"_,"r10"_,"r11"_,"r12"_,"r13"_,"r14"_,"r15"_};
    /**/  if(size==0) { if(r<4) return string(gprs[r][0])+"l"_; else return gprs[r]+"l"_; }
    else if(size==1) { if(r<8) return string(gprs[r]); else return gprs[r]+"w"_; }
    else if(size==2) { if(r<8) return "e"_+gprs[r]; else return gprs[r]+"d"_; }
    else if(size==3) { if(r<8) return "r"_+gprs[r]; else return string(gprs[r]); }
    error(r,size);
}
string xmm(int r, int) { return "xmm"_+str(r); }
string reg(int r, int size, int sse) { return sse ? xmm(r,size) : gpr(r,size); }

static constexpr ref<byte> ops[] = {"add"_,"or"_,"adc"_,"sbb"_,"and"_,"sub"_,"not"_,"cmp"_};
static constexpr ref<byte> ccs[] = { "o"_,"no"_,"c"_,"nc"_,"e"_,"ne"_,"na"_,"a"_,"s"_,"ns"_,"p"_,"np"_,"l"_,"ge"_,"le"_,"g"_};
static constexpr ref<byte> shs[] = {"rol"_,"ror"_,"rcl"_,"rcr"_,"shl"_,"shr"_,"sal"_,"sar"_};

int modrm(uint8 rex, int size, const byte*& c, string& s, int sse=0) {
    s<<'[';
    uint8 modrm = *c++;
    int mod = modrm>>6;
    int r = (modrm>>3)&0b111; if(rex&4) r+=8; //REX.r
    int rm = modrm&0b111; int b=rex&1;
    if(rm== rSP) { //SIB
        int sib = *c++;
        int scale = sib>>6;
        int index = (sib>>3)&0b111; if(rex&2) index+=8; //REX.x
        int base = sib&0b111; if(rex&1) base+=8; //REX.b
        if(base == rBP) s<<hex(imm(c,2),8); //[imm32 +
        else s<<gpr(base,size); //[base +
        if(index!=rSP) s<<'+'<<gpr(index,size)<<'*'<<str(1<<scale);
        if(mod==1) s<<'+'<<hex(imm(c,0),2);
        else if(mod==2) s<<'+'<<hex(imm(c,2),8);
    } else { //mod r/m
        if(mod == 0) {
            if(rm == rBP) s<<hex(imm(c,2),8); //[imm32]
            else s<< gpr(rm +(b?8:0),3); //[reg]
        }
        if(mod == 1 || mod == 2) { //[reg+imm]
            int i = imm(c,mod==1?0:2);
            s<< gpr(rm +(b?8:0),size)<<(i>0?"+"_:"-"_)+hex(abs(i),2<<(mod==1?0:2));
        }
        if(mod == 3) { //reg
            s=copy(reg(rm +(b?8:0),size,sse));
            return r; //avoid []
        }
    }
    s<<']';
    return r;
}

void disassemble(const ref<byte>& code) {
    for(const byte* c=code.begin(), *end=code.end();c<end;) {
        log_(hex(int(c-code.begin()),2)+":\t"_ /*+hex(ref<byte>(c,5))+"\t"_*/);
        uint8 op = *c++;
        int size=2/*operand size*//*, asize=3*//*address size*/, scalar=0;
        if(op==0x66) size=1, op = *c++; //for SSE size=2 -> float (ps), size=1 -> double (pd)
        else if(op==0x67) /*asize=2,*/ op = *c++; //use 32bit address size (for lea)
        else if(op==0xF2) size=1, scalar=1, op = *c++; //sd
        else if(op==0xF3) scalar=1, op = *c++; //ss
        uint8 rex=0; if((op&0xF0) == 0x40) { rex = op&0x0F; if(rex&8) size=3; op = *c++; }
        if(op==0x0F) {
            uint8 op = *c++;
            if(op==0x10 || op==0x28) { //mov[ua]p xmm, xmm/m
                string src; int r=modrm(rex,2,c,src,1);
                log( (op==0x10?"movup"_:"movap"_)+string("?sd?"[size])+" "_+xmm(r,size)+", "_+src );
            } else if(op==0x11) { //mov[ua]p xmm/m, xmm
                string src; int r=modrm(rex,2,c,src,1);
                log( (op==0x10?"movup"_:"movap"_)+string("?sd?"[size])+" "_+src+", "_+xmm(r,size) );
            } else if(op==0x12) { //movhlps xmm/m, xmm
                string src; int r=modrm(rex,2,c,src,1);
                log("movhlp"_+string("?sd?"[size])+" "_+src+", "_+xmm(r,size) );
            } else if(op==0x15) { //unpckhp xmm, xmm/m
                string src; int r=modrm(rex,2,c,src,1);
                log( "unpckhp"_+string("?sd?"[size])+" "_+xmm(r,size)+", "_+src );
            } else if(op==0x18) { //prefetch
                string src; int r=modrm(rex,2,c,src,1);
                log("prefetch"_+(r==0?string("nta"_):dec(r-1))+" "_+src);
            } else if(op==0x1F) { //nop
                string src; modrm(rex,2,c,src,1);
                log( "nop"_ );
            } else if(op==0x29) { //movap xmm/m, xmm
                string src; int r=modrm(rex,2,c,src,1);
                log( "movap"_+string("?sd?"[size])+" "_+src+", "_+xmm(r,size) );
            }  else if(op==0x2A || op==0x2C || op==0x2D) { //cvt r, xmm/m
                string src; int r=modrm(rex,2,c,src,!scalar);
                static constexpr ref<byte> lookup[] = {"cvti2f"_,"?"_,"cvttf2i"_,"cvtf2i"_};
                log( replace(replace(lookup[op-0x2A],"i"_,scalar?"si"_:"pi"_),"f"_,string((scalar?"s"_:"p"_)+(size==1?"d"_:"s"_)))+" "_+src+", "_+xmm(r,size) );
            } else if(op>=0x51&&op<=0x6D) { //op xmm, xmm/m
                string src; int r=modrm(rex,2,c,src,1);
                const ref<byte> lookup[] = {"sqrt"_,"rsqrt"_,"rcp"_,"and"_,"andn"_,"or"_,"xor"_,"add"_,"mul"_,
                                            size==1?"cvtpd2ps"_:"cvtps2pd"_,size==1?"cvtps2dq"_:"cvtdq2ps"_,
                                            "sub"_,"min"_,"div"_,"max"_,"punpcklbw"_,"punpcklwd"_,"punpckldq"_,"packsswb"_,
                                            "pcmpgtb"_,"pcmpgtw"_,"pcmpgtd"_,"packuswb"_,"punpckhbw"_,"punpckhwd"_,"punpckhdq"_,
                                            "packssdw"_,"punpcklqdq"_};
                log(lookup[op-0x51]+((op<=0x59||(op>=0x5C&&op<=0x5F))?string(str("ps"[scalar])+str("?sd?"[size])):string())+" "_+xmm(r,size)+", "_+src);
            } else if((op&0xF0)==0x80) { //jcc imm16/32
                int disp = imm(c,size);
                log("j"_+ccs[op&0xF]+" "_+ hex(int(c-code.begin())+disp,2<<size));
            } else if((op&0xF0)==0x90) { //setcc r/m8
                size=0;
                string dst; int r = modrm(rex,size,c,dst); if(r&7) error(r);
                log("set"_+ccs[op&0xF]+" "_+ dst);
            } else if(op==0xAF) { //imul r, r/m
                string src; int r = modrm(rex,size,c,src);
                log("imul "_+gpr(r,size)+", "_+src );
            } else if(op==0xB6||op==0xB7) {
                if(!(op&1)) size=0;
                string src; int r=modrm(rex,size,c,src);
                log("movzx"_+string("bwlq"[size])+" "_+gpr(r,size)+", "_+src );
            } else error("0F"_+hex(op));
        } else if(op<0x40 && (op&0x7)<=5) { //binary operator
            if(!(op&1)) size=0;
            if(op&4) { //op ax, imm
                int i=imm(c,size);
                log(ops[op>>3]+string("bwlq"[size])+" "_+gpr(0,size)+", "_+hex(i,2<<size) );
            } else {
                if(op&2) { //op r, r/m
                    string src; int r = modrm(rex,size,c,src);
                    log(ops[op>>3]+string("bwlq"[size])+" "_+gpr(r,size)+", "_+src );
                } else { //op r/m, r
                    string dst; int r = modrm(rex,size,c,dst);
                    log(ops[op>>3]+string("bwlq"[size])+" "_+dst+", "_+gpr(r,size) );
                }
            }
        } else if(op>=0x50&&op<0x60) {
            int r = op&7;
            log( "push"_+string("bwlq"[size])+" "_+gpr(r,size));
        } else if(op==0x63) { //movsxd r, r/m
            string src; int r=modrm(rex,size,c,src);
            log( "movsxd"_+" "_+gpr(r,size)+", "_+src );
        } else if((op&0xF0)==0x70) { //jcc imm8
            int disp = imm(c,0);
            log( "j"_+ccs[op&0xF]+" "_ + hex(int(c-code.begin())+disp,2) );
        } else if(op==0x80 || op==0x81) { //op imm8, r/m
            if(!(op&1)) size=0;
            string src; int r=modrm(rex,size,c,src); r&=7;
            log( ops[r]+string("bwlq"[size])+" "_+hex(imm(c,size==3?2:size),1<<size)+", "_+src );
        } else if(op==0x83) { //op r/m, imm8
            string src; int r=modrm(rex,size,c,src); r&=7;
            log( ops[r]+string("bwlq"[size])+" "_+src+", "_+hex(imm(c,0),2) );
        } else if(op==0x84 || op==0x85) { //test r, r/m
            if(!(op&1)) size=0;
            string src; int r=modrm(rex,size,c,src);
            log( "test"_+string("bwlq"[size])+" "_+gpr(r,size)+", "_+src );
        } else if(op==0x88 || op==0x89) { //mov r/m, r
            if(!(op&1)) size=0;
            string dst; int r=modrm(rex,size,c,dst);
            log( "mov"_+string("bwlq"[size])+" "_+dst+", "_+gpr(r,size) );
        } else if(op==0x8A || op==0x8B) { //mov r, r/m
            if(!(op&1)) size=0;
            string src; int r=modrm(rex,size,c,src);
            log( "mov"_+string("bwlq"[size])+" "_+gpr(r,size)+", "_+src );
        } else if(op==0x8D) { //lea r, m
            string m; int r = modrm(rex,size,c,m);
            log( "lea   "_+gpr(r,size)+", "_+m );
        } else if((op&0xF0)==0x90) { //xchg r, m
            log( "xchg   "_+gpr((op&0xF)+(rex&4?8:0), size)+", "_+gpr(0, size) );
        } else if(op==0x98) { //sign extend
            if(rex&0xC) log("cdqe rax, eax"_);
            else log("cwde eax, ax"_);
        } else if(op==0xC0 || op==0xC1) { //shx r/m, 1
            if(!(op&1)) size=0;
            string dst; int r=modrm(rex,size,c,dst); r&=7;
            log(shs[r]+" "_+dst+", "_+hex(imm(c,0),2));
        } else if(op==0xC6 || op==0xC7) { //mov r/m, imm
            if(!(op&1)) size=0;
            string dst; int r = modrm(rex,size,c,dst); if(r&7) error(r);
            log( "mov"_+string("bwlq"[size])+" "_+ dst + " , "_ + hex(imm(c,size),2<<size) );
        } else if(op==0xD0 || op==0xD1) { //shx r/m, 1
            if(!(op&1)) size=0;
            string dst; int r=modrm(rex,size,c,dst); r&=7;
            log(shs[r]+" "_+dst+", 1"_);
        } else if(op==0xD2 || op==0xD3) { //shx r/m, cl
            if(!(op&1)) size=0;
            string dst; int r=modrm(rex,size,c,dst); r&=7;
            log( shs[r]+" "_+dst+", cl"_ );
        } else if(op==0xE8) { //call imm32
            int disp = imm(c,size);
            log( "call  "_ + hex(int(c-code.begin())+disp,2));
        } else if(op==0xE9) { //jmp imm32
            int disp = imm(c,size);
            log( "jmp  "_ + hex(int(c-code.begin())+disp,2));
        } else if(op==0xEB) { //jmp imm8
            int disp = imm(c,0);
            log( "jmp  "_ + hex(int(c-code.begin())+disp,2) );
        } else if(op==0xF6 || op==0xF7) {
            if(!(op&1)) size=0;
            string s; int r = modrm(rex,size,c,s); r&=7;
            if(r==0||r==1) log("test "_+s+", "_+hex(imm(c,size),2<<size));
            else if(r==2) log("not "_+s);
            else if(r==3) log("neg "_+s);
            else error("F7"_,r);
        } else if(op==0xF8) {
            log("clc");
        } else if(op==0xFC) {
            log("cld");
        } else if(op==0xFF) {
            string s; int r = modrm(rex,size,c,s); r&=7;
            /**/  if(r==0) log("inc "_+s);
            else if(r==1) log("dec "_+s);
            else error("FF"_,r);
        } else error("Unkown opcode"_,hex(op));
        assert(end<=code.end());
    }
}
