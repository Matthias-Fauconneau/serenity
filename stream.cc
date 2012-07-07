#include "stream.h"
#include "string.h"

bool TextStream::matchAny(const array<byte>& any) {
    if(available(1)) { byte c=get(1)[0]; for(const byte& e: any) if(c == e) { advance(1); return true; } }
    return false;
}

bool TextStream::match(const string& key) {
    if(available(key.size())>=key.size() && get(key.size()) == key) { advance(key.size()); return true; }
    else return false;
}

void TextStream::whileAny(const array<byte>& any) { while(matchAny(any)){} }

array<byte> TextStream::until(const array<byte>& key) {
    array<byte> a;
    while(available(key.size())>=key.size()) { if(get(key.size()) == key) { advance(key.size()); break; } a << read(1); }
    return a;
}

array<byte> TextStream::untilAny(const array<byte>& any) {
    array<byte> a;
    while(available(1) && !matchAny(any)) a << read(1);
    return a;
}

string TextStream::untilEnd() { uint size=available(-1); return read(size); }

void TextStream::skip() { whileAny(" \t\n\r"_); }

string TextStream::word() {
    string word;
    for(;available(1);) { byte c=get(1)[0]; if(!(c>='a'&&c<='z')) break; word<<c; advance(1); }
    return word;
}

string TextStream::xmlIdentifier() {
    string identifier;
    for(;available(1);) {
        byte c=get(1)[0];
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
