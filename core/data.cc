#include "data.h"
#include "string.h"

bool Data::wouldMatch(uint8 key) {
    if(available(1) && (uint8)peek() == key) return true;
    else return false;
}
bool Data::wouldMatch(char key) {
    if(available(1) && peek() == key) return true;
    else return false;
}

bool Data::match(uint8 key) {
    if(wouldMatch(key)) { advance(1); return true; }
    else return false;
}
bool Data::match(char key) {
    if(wouldMatch(key)) { advance(1); return true; }
    else return false;
}

bool Data::wouldMatch(const ref<uint8>& key) {
    if(available(key.size)>=key.size && peek(key.size) == cast<byte>(key)) return true;
    else return false;
}
bool Data::wouldMatch(const string& key) {
    if(available(key.size)>=key.size && peek(key.size) == key) return true;
    else return false;
}

bool Data::match(const ref<uint8>& key) {
    if(wouldMatch(key)) { advance(key.size); return true; }
    else return false;
}
bool Data::match(const string& key) {
    if(wouldMatch(key)) { advance(key.size); return true; }
    else return false;
}

void Data::skip(const uint8 key) {
    if(!match(key)) error("Expected '"_+hex(key)+"', got '"_+hex((uint8)peek())+"'"_);
}
void Data::skip(const char key) {
    if(!match(key)) error("Expected '"_+string{key}+"', got '"_+peek(1)+"'"_);
}

void Data::skip(const ref<uint8>& key) {
    if(!match(key)) error("Expected '"_+hex(key)+"', got '"_+hex(peek(key.size))+"'"_);
}
void Data::skip(const string& key) {
    if(!match(key)) error("Expected '"_+key+"', got '"_+peek(key.size)+"'"_);
}

ref<uint8> BinaryData::whileNot(uint8 key) {
    uint start=index;
    while(available(1) && (uint8)peek() != key) advance(1);
    return cast<uint8>(slice(start, index-start));
}


void TextData::advance(uint step) {
    assert(index<buffer.size, index, buffer.size);
    for(uint start=index; index<start+step; index++) if(buffer[index]=='\n') lineIndex++;
}

char TextData::wouldMatchAny(const string& any) {
    if(!available(1)) return false;
    byte c=peek();
    for(const byte& e: any) if(c == e) return c;
    return 0;
}

string TextData::wouldMatchAny(const ref<string>& keys) {
    for(string key: keys) if(wouldMatch(key)) return key;
    return ""_;
}


char TextData::matchAny(const string& any) {
    char c = wouldMatchAny(any);
    if(c) advance(1);
    return c;
}

string TextData::matchAny(const ref<string>& keys) {
    for(string key: keys) if(match(key)) return key;
    return ""_;
}

bool TextData::matchNo(const string& any) {
    byte c=peek();
    for(const byte& e: any) if(c == e) return false;
    advance(1); return true;
}

string TextData::whileAny(char key) {
    uint start=index; while(match(key)) {} return slice(start, index-start);
}
string TextData::whileAny(const string& any) {
    uint start=index; while(matchAny(any)){} return slice(start,index-start);
}

string TextData::whileNot(char key) {
    uint start=index, end;
    for(;;advance(1)) {
        if(!available(1)) { end=index; break; }
        if(peek() == key) { end=index; break; }
    }
    return slice(start, end-start);
}
string TextData::whileNo(const string& any) {
    uint start=index; while(available(1) && matchNo(any)){} return slice(start,index-start);
}
string TextData::whileNo(const string& any, char left, char right) {
    uint start=index; int nest=0;
    while(available(1)) {
        /***/ if(match(left)) nest++;
        else if(peek()==right) { nest--; assert(nest>=0); advance(1); if(nest==0) break; }
        else if(!matchNo(any)) { if(nest==0) break; else advance(1); }
    }
    return slice(start,index-start);
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
    if(!s) error("Expected integer", line(), lineIndex);
    return fromInteger(s, 10);
}

int TextData::mayInteger(int defaultValue) {
    string s = whileInteger(true);
    return s ? fromInteger(s, 10): defaultValue;
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
    return fromInteger(whileHexadecimal(), 16);
}

string TextData::whileDecimal() {
    uint start=index;
    matchAny("-+"_);
    if(!match("∞"_)) for(bool gotDot=false, gotE=false;available(1);) {
        byte c=peek();
        /**/  if(c=='.') { if(gotDot||gotE) break; gotDot=true; advance(1); }
        else if(c=='e' || c=='E') { if(gotE) break; gotE=true; advance(1); if(peek()=='-' || peek()=='+') advance(1); }
        else if(c>='0'&&c<='9') advance(1);
        else break;
    }
    return slice(start,index-start);
}

double TextData::decimal() { return fromDecimal(whileDecimal()); }
