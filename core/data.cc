#include "data.h"
#include "string.h"

ref<byte> BinaryData::untilNull() {
    uint start=index;
    while(available(1) && next()){} assert(index>start);
    return Data::slice(start,index-1-start);
}

bool BinaryData::seekLast(const ref<byte>& key) {
    peek(-1); //try to completely read source
    for(index=buffer.size-key.size;index>0;index--) { if(peek(key.size) == key) return true; }
    return false;
}

bool TextData::match(char key) {
    if(available(1) && peek() == key) { advance(1); return true; }
    else return false;
}

bool TextData::match(const string& key) {
    if(available(key.size)>=key.size && peek(key.size) == key) { advance(key.size); return true; }
    else return false;
}

bool TextData::matchAny(const string& any) {
    if(!available(1)) return false;
    byte c=peek();
    for(const byte& e: any) if(c == e) { advance(1); return true; }
    return false;
}

bool TextData::matchNo(const string& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) return false;
    advance(1); return true;
}

void TextData::skip(const string& key) {
    assert_(match(key), "'"_+key+"'"_, /*"'"_+untilEnd()+"'"_*/ until('\n'));
}

string TextData::whileNot(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; break; }
    }
    return slice(start, end-start);
}
string TextData::whileAny(const string& any) {
    uint start=index; while(available(1) && matchAny(any)){} return slice(start,index-start);
}
string TextData::whileNo(const string& any) {
    uint start=index; while(available(1) && matchNo(any)){} return slice(start,index-start);
}

string TextData::until(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; advance(1); break; }
    }
    return slice(start, end-start);
}

string TextData::until(const string& key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(available(key.size)<key.size) { end=index; break; }
         if(peek(key.size) == key) { end=index; advance(key.size); break; }
    }
    return slice(start, end-start);
}

string TextData::untilAny(const string& any) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(matchAny(any)) { end=index-1; break; }
    }
    return slice(start,end-start);
}

string TextData::untilEnd() { uint size=available(-1); return read(size); }

void TextData::skip() { whileAny(" \t\n\r"_); }

string TextData::line() { return until('\n'); }

string TextData::word(const string& special) {
    uint start=index;
    for(;available(1);) { byte c=peek(); if(!(c>='a'&&c<='z' ) && !(c>='A'&&c<='Z') && !special.contains(c)) break; advance(1); }
    assert(index>=start, line());
    return slice(start,index-start);
}

string TextData::identifier(const string& special) {
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
    if(c!='\\') return c;
    c = peek();
    int i="\'\"nrtbf()\\"_.indexOf(c);
    if(i<0) { /*error("Invalid escape sequence '\\"_+str(c)+"'"_);*/ return '/'; }
    advance(1);
    return "\'\"\n\r\t\b\f()\\"[i];
}

string TextData::whileInteger(bool sign) {
    uint start=index;
    if(sign) matchAny("-+"_);
    for(;available(1);) {
        byte c=peek();
        if(c>='0'&&c<='9') advance(1); else break;
    }
    return slice(start,index-start);
}

int TextData::integer(bool sign) {
    string s = whileInteger(sign);
    assert(s, untilEnd());
    return toInteger(s, 10);
}

uint TextData::mayInteger() {
    string s = whileInteger(false);
    return s?toInteger(s, 10):-1;
}

string TextData::whileHexadecimal() {
    uint start=index;
    for(;available(1);) {
        byte c=peek();
        if((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')) advance(1); else break;
    }
    return slice(start,index-start);
}

uint TextData::hexadecimal() {
    return toInteger(whileHexadecimal(), 16);
}

string TextData::whileDecimal() {
    uint start=index;
    matchAny("-+"_);
    if(!match("∞"_)) for(bool gotDot=false, gotE=false;available(1);) {
        byte c=peek();
        /***/ if(c=='.') { if(gotDot||gotE) break; gotDot=true; advance(1); }
        else if(c=='e') { if(gotE) break; gotE=true; advance(1); if(peek()=='-') advance(1); }
        else if(c>='0'&&c<='9') advance(1);
        else break;
    }
    return slice(start,index-start);
}

double TextData::decimal() { return toDecimal(whileDecimal()); }
