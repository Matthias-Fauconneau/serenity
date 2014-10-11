#include "string.h"
#include "math.h"
#include "data.h"

// -- string

string str(const char* source) { size_t size=0; while(source[size]) size++; return string(source,size); }

bool operator <(const string a, const string b) {
    for(uint i: range(min(a.size,b.size))) {
        if(a[i] < b[i]) return true;
        if(a[i] > b[i]) return false;
    }
    return a.size < b.size;
}

bool operator <=(const string a, const string b) {
    for(uint i: range(min(a.size,b.size))) {
        if(a[i] < b[i]) return true;
        if(a[i] > b[i]) return false;
    }
    return a.size <= b.size;
}

bool startsWith(const string s, const string a) {
    return a.size<=s.size && string(s.data,a.size)==a;
}

bool endsWith(const string s, const string a) {
    return a.size<=s.size && string(s.data+s.size-a.size,a.size)==a;
}

bool find(const string s, const string a) {
    if(a.size>s.size) return false;
    for(uint i=0;i<=s.size-a.size;i++) if(string(s.data+i,a.size)==a) return true;
    return false;
}

string section(const string s, byte separator, int begin, int end) {
    if(!s) return "";
    uint b,e;
    if(begin>=0) { b=0; for(uint i=0;i<(uint)begin && b<s.size;b++) if(s[b]==separator) i++; }
    else { b=s.size; if(begin!=-1) { b--; for(uint i=0;b>0;b--) if(s[b]==separator) { i++; if(i>=uint(-begin-1)) { b++; break; } } } }
    if(end>=0) { e=0; for(uint i=0;e<s.size;e++) if(s[e]==separator) { i++; if(i>=(uint)end) break; } }
    else { e=s.size; if(end!=-1) { e--; for(uint i=0;e>0;e--) { if(s[e]==separator) { i++; if(i>=uint(-end-1)) break; } } } }
    assert(e>=b,s,separator,begin,end);
    return string(s.data+b,e-b);
}

bool isInteger(const string s) { if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }
int64 fromInteger(const string number, int base) { return TextData(number).integer(true, base); }
double fromDecimal(const string number) { return TextData(number).decimal(); }

// -- String

char lowerCase(char c) { return c>='A'&&c<='Z'?'a'+c-'A':c; }
String toLower(const string source) { return apply(source, lowerCase); }

char upperCase(char c) { return c>='a'&&c<='z'?'A'+c-'a':c; }
String toUpper(const string source) { return apply(source, upperCase); }

String left(const string source, size_t size, const char pad) {
    buffer<char> target(max(size, source.size));
    target.slice(0, source.size).copy(source);
    target.slice(source.size).clear(pad);
    return move(target);
}
String right(const string source, size_t size, const char pad) {
    buffer<char> target(max(size, source.size));
    target.slice(0, source.size).clear(pad);
    target.slice(source.size).copy(source);
    return move(target);
}

// -- string[]

String join(const ref<string> list, const string separator) {
    if(!list) return {};
    size_t size = 0; for(auto e: list) size += e.size;
    String target ( size + (list.size-1)*separator.size );
    for(size_t i: range(list.size)) { target.append( list[i] ); if(i<list.size-1) target.append( separator ); }
    return target;
}
String join(const ref<String> list, const string separator) { return join(toRefs(list), separator); }

array<string> split(const string source, byte separator) {
    array<string> list;
    TextData s (source);
    while(s) list.append( s.until(separator) );
    return list;
}

// -- Number conversions

String utoa(uint64 n, uint base, int pad, char padChar) {
    assert(base>=2 && base<=16);
    byte buf[64]; int i=64;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = padChar;
    return String(string(buf+i,64-i));
}

String itoa(int64 number, uint base, int pad, char padChar) {
    assert(base>=2 && base<=16);
    byte buf[64]; int i=64;
    uint64 n=abs(number);
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    if(number<0) buf[--i]='-';
    while(64-i<pad) buf[--i] = padChar;
    return String(string(buf+i,64-i));
}

String ftoa(double n, int precision, uint pad, int exponent) {
    bool sign = n<0; n=abs(n);
    if(__builtin_isnan(n)) return ::right("NaN", pad);
    if(n==::inf) return ::right("∞", pad+2);
    if(n==-::inf) return ::right("-∞", pad+2);
    int e=0; if(n && exponent && (n<1 || log10(n)>=precision+4)) e=floor(log10(n) / exponent) * exponent, n /= exp10(e);
    String s;
    if(sign) s.append('-');
    if(precision /*&& n!=round(n)*/) {
        double integer=1, fract=__builtin_modf(n, &integer);
        uint decimal = round(fract*exp10(precision));
        uint exp10=1; for(uint i unused: range(precision)) exp10*=10; // Integer exp10(precision)
        if(decimal==exp10) integer++, decimal=0; // Rounds to ceiling integer
        s.append( utoa(integer) );
        s.append('.');
        s.append( utoa(decimal, 10, precision,'0') );
    } else s.append( utoa(round(n)) );
    if(exponent==3 && e==3) s.append('K');
    else if(exponent==3 && e==6) s.append('M');
    else if(exponent==3 && e==9) s.append('G');
    else if(e) { s.append('e'); s.append(itoa(e)); }
    return pad > s.size ? right(s, pad) : move(s);
}

String str(float n) { return (isNumber(n) && n==round(n)) ? dec(int(n)) : ftoa(n); }
String str(double n) { return (isNumber(n) && n==round(n)) ? dec(int(n)) : ftoa(n); }
