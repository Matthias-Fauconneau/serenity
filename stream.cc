#include "stream.h"
#include "string.h"
#include "debug.h"

ref<byte> DataStream::untilNull() { uint start=index; while(available(1) && next()){} assert(index>start); return Stream::slice(start,index-1-start); }

bool DataStream::seekLast(const ref<byte>& key) {
    get(-1); //try to completely read source
    for(index=buffer.size()-key.size;index>0;index--) { if(get(key.size) == key) return true; }
    return false;
}

bool TextStream::match(char key) {
    if(available(1) && peek() == key) { advance(1); return true; }
    else return false;
}

bool TextStream::match(const ref<byte>& key) {
    if(available(key.size)>=key.size && get(key.size) == key) { advance(key.size); return true; }
    else return false;
}

bool TextStream::matchAny(const ref<byte>& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) { advance(1); return true; }
    return false;
}

bool TextStream::matchNo(const ref<byte>& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) return false;
    advance(1); return true;
}

ref<byte> TextStream::whileNot(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; break; }
    }
    return slice(start, end-start);
}
ref<byte> TextStream::whileAny(const ref<byte>& any) {
    uint start=index; while(available(1) && matchAny(any)){} return slice(start,index-start);
}
ref<byte> TextStream::whileNo(const ref<byte>& any) {
    uint start=index; while(available(1) && matchNo(any)){} return slice(start,index-start);
}

ref<byte> TextStream::until(char key) { invariant();
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; advance(1); break; }
    }
    return slice(start, end-start);
}

ref<byte> TextStream::until(const ref<byte>& key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(available(key.size)<key.size) { end=index; break; }
         if(get(key.size) == key) { end=index; advance(key.size); break; }
    }
    return slice(start, end-start);
}

ref<byte> TextStream::untilAny(const ref<byte>& any) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(matchAny(any)) { end=index-1; break; }
    }
    return slice(start,end-start);
}

ref<byte> TextStream::untilEnd() { uint size=available(-1); return read(size); }

void TextStream::skip() { whileAny(" \t\n\r"_); }

ref<byte> TextStream::word() {
    uint start=index;
    for(;available(1);) { byte c=peek(); if(!(c>='a'&&c<='z')) break; advance(1); }
    return slice(start,index-start);
}

ref<byte> TextStream::identifier() {
    uint start=index;
    for(;available(1);) {
        byte c=peek();
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_')) break;
        advance(1);
    }
    return slice(start,index-start);
}

int TextStream::number(uint base) {
    uint start=index;
    for(;available(1);) {
        byte c=peek();
        if((c>='0'&&c<='9')||(base==16&&((c>='a'&&c<='f')||(c>='A'&&c<='F')))) advance(1); else break;
    }
    if(start==index) return -1;
    return toInteger(slice(start,index-start), base);
}

char TextStream::character() {
    if(!match('\\')) return next();
    /***/ if(match('n')) return '\n';
    else if(match('"')) return '"';
    else if(match('\'')) return '\'';
    else if(match('\\')) return '\\';
    else error("Invalid escape character"_,(char)peek());
}
