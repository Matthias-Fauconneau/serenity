#include "stream.h"
#include "string.h"
#include "array.cc"

bool TextStream::matchAny(const ref<byte>& any) {
    if(available(1)) { byte c=peek(); for(const byte& e: any) if(c == e) { advance(1); return true; } }
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

void TextStream::whileAny(const ref<byte>& any) { while(matchAny(any)){} }

ref<byte> TextStream::until(const ref<byte>& key) {
    int start=index, end=index;
    while(available(key.size)>=key.size) { if(get(key.size) == key) { advance(key.size); break; } end=index; }
    return slice(start, end);
}

ref<byte> TextStream::untilAny(const ref<byte>& any) {
    int start=index, end=index;
    while(available(1) && !matchAny(any)) end=index;
    return slice(start,end);
}

ref<byte> TextStream::untilEnd() { uint size=available(-1); return read(size); }

void TextStream::skip() { whileAny(" \t\n\r"_); }

ref<byte> TextStream::word() {
    int start=index,end=index;
    for(;available(1);) { byte c=peek(); if(!(c>='a'&&c<='z')) break; advance(1); end=index; }
    return slice(start,end);
}

ref<byte> TextStream::xmlIdentifier() {
    string identifier;
    for(;available(1);) {
        byte c=peek();
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_')) break;
        identifier<<c;
        advance(1);
    }
    return identifier;
}

int TextStream::number(int base) {
    string number;
    for(;available(1);) {
        byte c=peek();
        if((c>='0'&&c<='9')||(base==16&&((c>='a'&&c<='f')||(c>='A'&&c<='F')))) number<<c, advance(1); else break;
    }
    if(!number) return -1;
    return toInteger(number, base);
}
