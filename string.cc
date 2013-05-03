#include "string.h"

/// ref<byte>

bool operator <(const ref<byte>& a, const ref<byte>& b) {
    for(uint i: range(min(a.size,b.size))) {
        if(a[i] < b[i]) return true;
        if(a[i] > b[i]) return false;
    }
    return a.size > b.size;
}

ref<byte> str(const char* s) {
    if(!s) return "null"_; int i=0; while(s[i]) i++; return ref<byte>((byte*)s,i);
}

bool startsWith(const ref<byte>& s, const ref<byte>& a) {
    return a.size<=s.size && ref<byte>(s.data,a.size)==a;
}

bool find(const ref<byte>& s, const ref<byte>& a) {
    if(a.size>s.size) return false;
    for(uint i=0;i<=s.size-a.size;i++) {
        if(ref<byte>(s.data+i,a.size)==a) return true;
    }
    return false;
}

bool endsWith(const ref<byte>& s, const ref<byte>& a) {
    return a.size<=s.size && ref<byte>(s.data+s.size-a.size,a.size)==a;
}

ref<byte> section(const ref<byte>& s, byte separator, int begin, int end) {
    if(!s) return ""_;
    uint b,e;
    if(begin>=0) { b=0; for(uint i=0;i<(uint)begin && b<s.size;b++) if(s[b]==separator) i++; }
    else { b=s.size; if(begin!=-1) { b--; for(uint i=0;b>0;b--) if(s[b]==separator) { i++; if(i>=uint(-begin-1)) { b++; break; } } } }
    if(end>=0) { e=0; for(uint i=0;e<s.size;e++) if(s[e]==separator) { i++; if(i>=(uint)end) break; } }
    else { e=s.size; if(end!=-1) { e--; for(uint i=0;e>0;e--) { if(s[e]==separator) { i++; if(i>=uint(-end-1)) break; } } } }
    assert(e>=b,s,separator,begin,end);
    return ref<byte>(s.data+b,e-b);
}

ref<byte> trim(const ref<byte>& s) {
    int begin=0,end=s.size;
    for(;begin<end;begin++) { byte c=s[begin]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
    for(;end>begin;end--) { uint c=s[end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
    return s.slice(begin, end-begin);
}

bool isInteger(const ref<byte>& s) {
    if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true;
}

long toInteger(const ref<byte>& number, int base) {
    assert(base>=2 && base<=16);
    assert(number);
    int sign=1;
    const byte* i = number.begin();
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    long value=0;
    for(;i!=number.end();++i) {
        int n;
        if(*i>='0' && *i<='9') n = *i-'0';
        else if(*i>='a' && *i<='f') n = *i+10-'a';
        else if(*i>='A' && *i<='F') n = *i+10-'A';
        else if(*i == '.') { error("Unexpected decimal"); break; }
        else break;
        value *= base;
        value += n;
    }
    return sign*value;
}

double toDecimal(const ref<byte>& number) {
    if(!number) return __builtin_nan("");
    double sign=1;
    const byte* i = number.begin();
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    double exponent = 1;
    double significand = 0;
    for(bool gotDot=false;i!=number.end();++i) {
        if(*i == '.') { gotDot=true; continue; }
        if(*i<'0' || *i>'9') { error("Unexpected",*i); break; }
        int n = *i-'0';
        significand *= 10;
        significand += n;
        if(gotDot) exponent *= 10;
    }
    return sign*significand/exponent;
}


/// string

array<ref<byte>> split(const ref<byte>& str, byte sep) {
    array<ref<byte>> list;
    const byte* b=str.begin();
    const byte* end=str.end();
    for(;;) {
        auto e = b;
        while(e!=end && *e!=sep) ++e;
        if(b==end) break;
        list << ref<byte>(b,e-b);
        if(e==end) break;
        b = e;
        if(b!=end && *b==sep) ++b;
    }
    return list;
}

string join(const ref<string>& list, const ref<byte>& separator) {
    string str;
    for(uint i: range(list.size)) { str<< list[i]; if(i<list.size-1) str<<separator; }
    return str;
}

string replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after) {
    string r(s.size);
    for(uint i=0; i<s.size;) {
        if(i<=s.size-before.size && string(s.data+i, before.size)==before) { r<<after; i+=before.size; }
        else { r << s[i]; i++; }
    }
    return r;
}

string toLower(const ref<byte>& s) {
    string lower;
    for(char c: s) if(c>='A'&&c<='Z') lower<<'a'+c-'A'; else lower << c;
    return lower;
}

string simplify(string&& s) {
    for(uint i=0; i<s.size;) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim heading
    for(uint i=0; i<s.size;) {
        byte c=s[i];
        if(c=='\r') { s.removeAt(i); continue; } //Removes any \r
        i++;
        if(c==' '||c=='\t'||c=='\n') while(i<s.size) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //Simplify whitespace
    }
    for(int i=s.size-1;i>0;i--) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim trailing
    return move(s);
}

string repeat(const ref<byte>& s, uint times) {
    string r; for(uint unused i: range(times)) r<<s; return r;
}

stringz strz(const ref<byte>& s) { stringz r; r.reserve(s.size); r<<s<<0; return r; }

/// Number conversions

template<uint base> string utoa(uint64 n, int pad) {
    assert(base>=2 && base<=16);
    byte buf[64]; int i=64;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = '0';
    return string(ref<byte>(buf+i,64-i));
}
template string utoa<2>(uint64,int);
template string utoa<16>(uint64,int);

template<uint base> string itoa(int64 number, int pad) {
    assert(base>=2 && base<=16);
    byte buf[64]; int i=64;
    uint64 n=abs(number);
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    if(number<0) buf[--i]='-';
    while(64-i<pad) buf[--i] = '0';
    return string(ref<byte>(buf+i,64-i));
}
template string itoa<10>(int64,int);

inline double exp(double x) { return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }
inline double exp10(double x) { return exp(x*ln(10)); }
inline double log10(double x) { return __builtin_log10(x); }
inline double round(double x) { return __builtin_round(x); }
string ftoa(double n, int precision, int pad, bool exponent) {
    bool sign = n<0; n=abs(n);
    if(__builtin_isnan(n)) return string("NaN"_);
    if(n==__builtin_inff()) return string("∞"_);
    if(n==-__builtin_inff()) return string("-∞"_);
    int e=0; if(n && (exponent || int(n*exp10(precision))==0)) e=round(log10(n)), n /= exp10(e);
    uint64 m=1; for(int i=0;i<precision;i++) m*=10;
    double integer=1, fract=__builtin_modf(n, &integer);
    string s;
    if(sign) s<<'-';
    s << utoa(integer,pad);
    if(precision) s <<'.'<< utoa<10>(round(fract*exp10(precision)),precision);
    if(e) s<<'e'<<itoa<10>(e);
    return move(s);
}
