#include "string.h"
#include "math.h" //isnan
/// string

string operator "" _(const char* data, size_t size) { return string(data,size); }

string toString(int number, int base, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    char buf[32]; int i=32;
    uint n = abs(number);
    //if(n >= 0x100000) { base=16; pad=8; }
    do {
        buf[--i] = "0123456789ABCDEF"[n%base];
        n /= base;
    } while( n!=0 );
    while(32-i<pad) buf[--i] = '0';
    if(number<0) buf[--i] = '-';
    return copy(string(buf+i,32-i));
}
string toString(float n, int precision, int base) {
    if(isnan(n)) return "NaN"_;
    if(isinf(n)) return n>0?"∞"_:"-∞"_;
    int m=1; for(int i=0;i<precision;i++) m*=base;
    return (n>=0?""_:"-"_)+toString(int(abs(n)),base)+"."_+toString(int(m*abs(n))%m,base,precision);
}
long readInteger(const char*& s, int base) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    int neg=0;
    if(*s == '-' ) s++, neg=1; else if(*s == '+') s++;
    long number=0;
    for(;*s;s++) {
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid integer",str);
        number *= base;
        number += n;
    }
    return neg ? -number : number;
}
long toInteger(const string& str, int base) { const char* s=&str; return readInteger(s,base); }
double readFloat(const char*& s, int base ) {
    assert(base>2 && base<16,"Unsupported base"_,base);
    int neg=0;
    if(*s == '-') s++, neg=1; else if(*s == '+') s++;
    int exponent = 1;
    int significand = 0;
    for(bool gotDot=false;*s;s++) {
        if(*s == '.') { gotDot=true; continue; }
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid float",str);
        significand *= base;
        significand += n;
        if(gotDot) exponent *= base;
    }
    return neg ? -float(significand)/float(exponent) : float(significand)/float(exponent);
}
double readFloat(const string& str, int base ) { const char* s=&str; return readFloat(s,base); }

string section(const string& s, char sep, int start, int end) {
    int b,e;
    if(start>=0) {
        b=0;
        for(int i=0;i<start && b<s.size;b++) if(s[b]==sep) i++;
    } else {
        b=s.size;
        if(start!=-1) for(int i=0;b-->0;) { if(s[b]==sep) { i++; if(i>=-start-1) break; } }
        b++; //skip separator
    }
    if(end>=0) {
        e=0;
        for(int i=0;e<s.size;e++) if(s[e]==sep) { i++; if(i>=end) break; }
    } else {
        e=s.size;
        if(end!=-1) for(int i=0;e-->0;) { if(s[e]==sep) { i++; if(i>=-end-1) break; } }
    }
    return s.slice(b,e-b);
}
string strz(const string& s) { return s+"\0"_; }
string strz(const char* s) { if(!s) return "null"_; int i=0; while(s[i]) i++; return string(s,i); }

array<string> split(const string& str, char sep) {
    array<string> r;
    int b=0,e=0;
    for(;;) {
        while(b<str.size && str[b]==sep) b++;
        e=b;
        while(e<str.size && str[e]!=sep) e++;
        if(b==str.size) break;
        r << str.slice(b,e-b);
        if(e==str.size) break;
        b=e+1;
    }
    return r;
}

/// log

#include "file.h"
template<> void log_(const bool& b) { log_(b?"true"_:"false"_); }
template<> void log_(const char& c) { log_(string(&c,1)); }
template<> void log_(const int& n) { log_(toString(n)); }
template<> void log_(const float& n) { log_(toString(n)); }
template<> void log_(const string& s) { write(1,s); }

/// Stream (TODO -> stream.cc)

//long TextStream::readInteger(int base) { auto b=(const char*)&data[pos], e=b; long r = ::readInteger(e,base); pos+=int(e-b); return r; }
//double TextStream::readFloat(int base) { auto b=(const char*)&data[pos], e=b; double r = ::readFloat(e,base); pos+=int(e-b); return r; }
