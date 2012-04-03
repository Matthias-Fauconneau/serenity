#include "string.h"
#include "math.h" //isnan/isinf

#include "array.cc"
template struct array<string>;
template int indexOf(const array<string>&, const string&);
template void insertSorted(array<string>&, string&&);

/// utf8_iterator
uint utf8_iterator::operator* () const {
    ubyte code = pointer[0];
    /**/  if((code&0b10000000)==0b00000000) return code;
    else if((code&0b11100000)==0b11000000) return(code&0b11111)<<6  |(pointer[1]&0b111111);
    else if((code&0b11110000)==0b11100000) return(code&0b01111)<<12|(pointer[1]&0b111111)<<6  |(pointer[2]&0b111111);
    else if((code&0b11111000)==0b11110000) return(code&0b00111)<<18|(pointer[1]&0b111111)<<12|(pointer[2]&0b111111)<<6|(pointer[3]&0b111111);
    else return code; //Windows-1252
}
const utf8_iterator& utf8_iterator::operator++() {
    ubyte code = *pointer;
    /**/  if((code&0b10000000)==0b00000000) pointer+=1;
    else if((code&0b11100000)==0b11000000) pointer+=2;
    else if((code&0b11110000)==0b11100000) pointer+=3;
    else if((code&0b11111000)==0b11110000) pointer+=4;
    else pointer+=1; //Windows-1252
    return *this;
}
const utf8_iterator& utf8_iterator::operator--() {
    ubyte code = *--pointer;
    if(code>=128) {
        //assert((code&0b11000000)==0b10000000,bin(code,8),(char)code,code,hex(code));
        if((code&0b11000000)!=0b10000000) {} //Windows-1252
        else { //UTF-8
            int i=0; for(;(code&0b11000000)==0b10000000;i++) code = *(--pointer);
#define WINDOWS_1252 1
            if(i==1) { if((code&0b11100000)!=0b11000000) pointer++; }
            else if(i==2) { if((code&0b11110000)!=0b11100000) pointer+=2; }
            else if(i==3) { if((code&0b11111000)!=0b11110000) pointer+=3; }
            else if(i==4) { if((code&0b11111100)!=0b11111000) pointer+=4; }
            else if(i==5) { if((code&0b11111110)!=0b11111100) pointer+=5; }
            else error(i);
#if WINDOWS_1252
#else
            /**/  if(i==1) assert((code&0b11100000)==0b11000000);
            else if(i==2) assert((code&0b11110000)==0b11100000);
            else if(i==3) assert((code&0b11111000)==0b11110000);
            else if(i==4) assert((code&0b11111100)==0b11111000);
            else if(i==5) assert((code&0b11111110)==0b11111100);
            else error(i);
#endif
        }
    }
    return *this;
}

/// string operations

bool startsWith(const array<byte> &s, const array<byte> &a) { return a.size()<=s.size() && string(s.data(),a.size())==a; }
bool contains(const string& s, const string& a) {
    if(a.size()>s.size()) return false;
    for(uint i=0;i<=s.size()-a.size();i++) {
        if(string(s.data()+i,a.size())==a) return true;
    }
    return false;
}
bool endsWith(const array<byte>& s, const array<byte>& a) { return a.size()<=s.size() && string(s.data()+s.size()-a.size(),a.size())==a; }

bool operator <(const string& a, const string& b) {
    for(uint i=0;i<min(a.size(),b.size());i++) {
        if(a[i] > b[i]) return false;
        if(a[i] < b[i]) return true;
    }
    return a.size() < b.size();
}

string strz(const string& s) { return s+"\0"_; }
string strz(const char* s) { if(!s) return "null"_; int i=0; while(s[i]) i++; return copy(string(s,i)); }

string section(const string& s, uint separator, int start, int end, bool includeSeparator) {
    if(!s) return ""_;
    uint b,e;
    if(start>=0) {
        b=0;
        utf8_iterator it=s.begin(); for(uint i=0;i<(uint)start && it!=s.end();++it,b++) if(*it==separator) i++;
    } else {
        b=s.size();
        if(start!=-1) {
            utf8_iterator it=s.end(); --it; --b;
            for(uint i=0;;--it,--b) { if(*it==separator) { i++; if(i>=uint(-start-1)) { if(!includeSeparator) b++; break; } } if(it == s.begin()) break; }
        }
    }
    if(end>=0) {
        e=0;
        utf8_iterator it=s.begin(); for(uint i=0;it!=s.end();++it,e++) if(*it==separator) { i++; if(i>=(uint)end) { if(includeSeparator) e++; break; } }
    } else {
        e=s.size();
        if(end!=-1) {
            utf8_iterator it=s.end(); --it; --e;
            for(uint i=0;;--it,--e) { if(*it==separator) { i++; if(i>=uint(-end-1)) { if(includeSeparator) e++; break; } } if(it == s.begin()) break; }
        }
    }
    assert(e>=b,"'"_+s+"'"_,separator,start,end,includeSeparator,e,b);
    return copy(string(s.data()+b,e-b));
}

array<string> split(const string& str, uint sep) {
    array<string> list;
    utf8_iterator b=str.begin();
    utf8_iterator end=str.end();
    for(;;) {
        while(b!=end && *b==sep) ++b;
        utf8_iterator e = b;
        while(e!=end && *e!=sep) ++e;
        if(b==end) break;
        list << copy(string(b,e));
        if(e==end) break;
        b = ++e;
    }
    return list;
}

string join(const array<string>& list, const string& separator) {
    string str;
    for(uint i=0;i<list.size();i++) { str<<list[i]; if(i<list.size()-1) str<<separator; }
    return str;
}

array<char> replace(const array<char>& s, const array<char>& before, const array<char>& after) {
    array<char> r(s.size());
    for(uint i=0;i<s.size();) {
        if(i<=s.size()-before.size() && string(s.data()+i, before.size())==before) { r<<after; i+=before.size(); }
        else { r << s[i]; i++; }
    }
    return r;
}

string toLower(const string& s) {
    string lower;
    for(auto c: s) if(c>='A'&&c<='Z') lower<<'a'+c-'A'; else lower << c;
    return lower;
}

string trim(const array<byte>& s) {
    int i=0,end=s.size();
    for(;i<end;i++) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
    for(;end>i;end--) { uint c=s[end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
    return slice(s, i, end-i);
}

string simplify(const array<byte>& s) {
    string simple;
    for(int i=0,end=s.size();i<end;) { //trim duplicate
        byte c=s[i];
        if(c=='\r') { i++; continue; }
        simple << c;
        i++;
        if(c==' '||c=='\t'||c=='\n') for(;i<end;i++) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; }
    }
    return simple;
}

string utf8(uint c) {
    string utf8;
    /**/  if(c<(1<<7))           utf8                                                                                                            << c;
    else if(c<(1<<(7+6)))     utf8                                             << (0b11000000|(c>>6))                      << (0b10000000|(c&0b111111));
    else if(c<(1<<(7+6+6))) utf8 << (0b11100000|(c>>12)) << (0b10000000|((c>>6)&0b111111)) << (0b10000000|(c&0b111111));
    else error(c);
    return utf8;
}

/// Human-readable value representation

string itoa(int64 number, int base, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    char buf[64]; int i=64;
    uint64 n=abs(number);
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = '0';
    if(number<0) buf[--i]='-',
    assert(i>=0);
    return copy(string(buf+i,64-i));
}

string ftoa(float n, int precision, int base) {
    if(isnan(n)) return "NaN"_;
    if(isinf(n)) return n>0?"∞"_:"-∞"_;
    int m=1; for(int i=0;i<precision;i++) m*=base;
    return (n>=0?""_:"-"_)+itoa(abs(n),base)+"."_+itoa(int(m*abs(n))%m,base,precision);
}

bool isInteger(const string& s) { if(!s) return false; for(auto c: s) if(c<'0'||c>'9') return false; return true; }

long toInteger(const string& number, int base) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    int sign=1;
    utf8_iterator i=number.begin();
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    long value=0;
    for(;i!=number.end();++i) {
        if(*i==' ') break;
        assert((*i>='0' && *i<='9')||(*i>='a'&&*i<='f'),"Invalid input '"_+number+"'"_);
        int n = indexOf(array<char>("0123456789abcdef",base), (char)*i);
        value *= base;
        value += n;
    }
    return sign*value;
}
