#include "data.h"
#include "string.h"
#include "debug.h"

ref<byte> BinaryData::untilNull() { uint start=index; while(available(1) && next()){} assert_(index>start); return Data::slice(start,index-1-start); }

bool BinaryData::seekLast(const ref<byte>& key) {
    peek(-1); //try to completely read source
    for(index=buffer.size()-key.size;index>0;index--) { if(peek(key.size) == key) return true; }
    return false;
}

bool TextData::match(char key) {
    if(available(1) && peek() == key) { advance(1); return true; }
    else return false;
}

bool TextData::match(const ref<byte>& key) {
    if(available(key.size)>=key.size && peek(key.size) == key) { advance(key.size); return true; }
    else return false;
}

bool TextData::matchAny(const ref<byte>& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) { advance(1); return true; }
    return false;
}

bool TextData::matchNo(const ref<byte>& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) return false;
    advance(1); return true;
}

ref<byte> TextData::whileNot(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; break; }
    }
    return slice(start, end-start);
}
ref<byte> TextData::whileAny(const ref<byte>& any) {
    uint start=index; while(available(1) && matchAny(any)){} return slice(start,index-start);
}
ref<byte> TextData::whileNo(const ref<byte>& any) {
    uint start=index; while(available(1) && matchNo(any)){} return slice(start,index-start);
}

ref<byte> TextData::until(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; advance(1); break; }
    }
    return slice(start, end-start);
}

ref<byte> TextData::until(const ref<byte>& key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(available(key.size)<key.size) { end=index; break; }
         if(peek(key.size) == key) { end=index; advance(key.size); break; }
    }
    return slice(start, end-start);
}

ref<byte> TextData::untilAny(const ref<byte>& any) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(matchAny(any)) { end=index-1; break; }
    }
    return slice(start,end-start);
}

ref<byte> TextData::untilEnd() { uint size=available(-1); return read(size); }

void TextData::skip() { whileAny(" \t\n\r"_); }

ref<byte> TextData::word() {
    uint start=index;
    for(;available(1);) { byte c=peek(); if(!(c>='a'&&c<='z' ) && !(c>='A'&&c<='Z')) break; advance(1); }
    return slice(start,index-start);
}

ref<byte> TextData::identifier(const ref<byte>& special) {
    uint start=index;
    for(;available(1);) {
        byte c=peek();
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||special.contains(c))) break;
        advance(1);
    }
    return slice(start,index-start);
}

char TextData::character() {
    byte c = next();
    if(c=='\\') return c;
    int i="\'\"nrtbf()\\"_.indexOf(c);
    if(i<0) { error("Invalid escape sequence"_,c); return '?'; }
    return "\'\"\n\r\t\b\f()\\"[i];
}

int TextData::integer(uint base) {
    uint start=index;
    for(;available(1);) {
        byte c=peek();
        if((c>='0'&&c<='9')||(base==16&&((c>='a'&&c<='f')||(c>='A'&&c<='F')))) advance(1); else break;
    }
    if(start==index) return -1;
    return toInteger(slice(start,index-start), base);
}

double TextData::number(uint base) {
    assert(base>2 && base<16,"Unsupported base"_,base);
    int neg=0;
    if(match('-')) neg=1; else match('+');
    int exponent = 1;
    int significand = 0;
    for(bool gotDot=false;available(1);) {
        if(match('.')) { gotDot=true; continue; }
        int n = "0123456789ABCDEF"_.indexOf(next());
        if(n < 0) break;
        significand *= base;
        significand += n;
        if(gotDot) exponent *= base;
    }
    return neg ? -double(significand)/double(exponent) : double(significand)/double(exponent);
}
