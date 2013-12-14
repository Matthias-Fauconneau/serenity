#include "string.h"
#include "math.h"

/// string

bool operator <(const string& a, const string& b) {
    for(uint i: range(min(a.size,b.size))) {
        if(a[i] < b[i]) return true;
        if(a[i] > b[i]) return false;
    }
    return a.size > b.size;
}

uint count(const string& a, byte c) {
    uint count=0; for(byte b: a) if(b==c) count++; return count;
}

string str(const char* s) {
    if(!s) return "null"_; int i=0; while(s[i]) i++; return string((byte*)s,i);
}

bool startsWith(const string& s, const string& a) {
    return a.size<=s.size && string(s.data,a.size)==a;
}

bool find(const string& s, const string& a) {
    if(a.size>s.size) return false;
    for(uint i=0;i<=s.size-a.size;i++) {
        if(string(s.data+i,a.size)==a) return true;
    }
    return false;
}

bool endsWith(const string& s, const string& a) {
    return a.size<=s.size && string(s.data+s.size-a.size,a.size)==a;
}

string section(const string& s, byte separator, int begin, int end) {
    if(!s) return ""_;
    uint b,e;
    if(begin>=0) { b=0; for(uint i=0;i<(uint)begin && b<s.size;b++) if(s[b]==separator) i++; }
    else { b=s.size; if(begin!=-1) { b--; for(uint i=0;b>0;b--) if(s[b]==separator) { i++; if(i>=uint(-begin-1)) { b++; break; } } } }
    if(end>=0) { e=0; for(uint i=0;e<s.size;e++) if(s[e]==separator) { i++; if(i>=(uint)end) break; } }
    else { e=s.size; if(end!=-1) { e--; for(uint i=0;e>0;e--) { if(s[e]==separator) { i++; if(i>=uint(-end-1)) break; } } } }
    assert(e>=b,s,separator,begin,end);
    return string(s.data+b,e-b);
}

string trim(const string& s) {
    int begin=0,end=s.size;
    for(;begin<end;begin++) { byte c=s[(uint)begin]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
    for(;end>begin;end--) { uint c=s[(uint)end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
    return s.slice(begin, end-begin);
}

bool isASCII(const string& s) {
    for(uint8 c : s) if(c<32 || c>126) return false;
    return true;
}

bool isUTF8(const string& s) {
    for(uint i=0; i<s.size;i++) {
        /**/  if((s[i]&0b10000000)==0b00000000) {}
        else if((s[i]&0b11100000)==0b11000000) for(uint j unused: range(1)) if((s[++i]&0b11000000) != 0b10000000) return false;
        else if((s[i]&0b11110000)==0b11100000) for(uint j unused: range(2)) if((s[++i]&0b11000000) != 0b10000000) return false;
        else if((s[i]&0b11111000)==0b11110000) for(uint j unused: range(3)) if((s[++i]&0b11000000) != 0b10000000) return false;
        else return false;
    }
    return true;
}

bool isInteger(const string& s) {
    if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true;
}

int64 toInteger(const string& number, int base) {
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

bool isDecimal(const string& number) {
    if(!number) return false;
    const byte* i = number.begin();
    if(*i == '-' || *i == '+') ++i;
    for(bool gotDot=false, gotE=false;i!=number.end();++i) {
        /**/  if(!gotDot && *i == '.') gotDot=true;
        else if(!gotE && *i == 'e') gotE=true;
        else if(*i<'0' || *i>'9') return false;
    }
    return true;
}

double toDecimal(const string& number) { //FIXME: fromDecimal
    if(!number) return __builtin_nan("");
    if(number == "∞"_) return __builtin_inf();
    double sign=1, eSign=1;
    const byte* i = number.begin();
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    double significand=0, decimal=0, exponent=0;
    for(bool gotDot=false, gotE=false;i!=number.end();) {
        if(!gotDot && *i == '.') { ++i; gotDot=true; continue; }
        if(!gotE && *i == 'e') { ++i; gotE=true; if(*i == '-' ) ++i, eSign=-1; else if(*i == '+') ++i; continue; }
        if(*i<'0' || *i>'9') { error("toDecimal('"_+number+"'') Unexpected '"_+str(*i)+"'"_); break; }
        int n = *i-'0';
        if(gotE) {
            exponent *= 10;
            exponent += n;
        } else {
            significand *= 10;
            significand += n;
            if(gotDot) decimal++;
        }
        ++i;
    }
    return sign*significand*exp10(eSign*exponent-decimal);
}

/// String

String join(const ref<string>& list, const string& separator) {
    String str;
    for(uint i: range(list.size)) { str<< list[i]; if(i<list.size-1) str<<separator; }
    return str;
}
String join(const ref<String>& list, const string& separator) { return join(toRefs(list),separator); }

String replace(const string& s, const string& before, const string& after) {
    String r(s.size);
    for(uint i=0; i<s.size;) {
        if(i<=s.size-before.size && string(s.data+i, before.size)==before) { r<<after; i+=before.size; }
        else { r << s[i]; i++; }
    }
    return r;
}

char toLower(char c) { return c>='A'&&c<='Z'?'a'+c-'A':c; }
String toLower(const string& s) { String lower(s.size); for(char c: s) lower<<toLower(c); return lower; }

char toUpper(char c) { return c>='a'&&c<='z'?'A'+c-'a':c; }
String toUpper(const string& s) { String upper(s.size); for(char c: s) upper<<toUpper(c); return upper; }

String simplify(String&& s) {
    for(uint i=0; i<s.size;) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim heading
    for(uint i=0; i<s.size;) {
        byte c=s[i];
        if(c=='\r') { s.removeAt(i); continue; } //Removes any \r
        i++;
        if(c==' '||c=='\t'||c=='\n') while(i<s.size) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //Simplify whitespace
    }
    for(size_t i=s.size-1;i>0;i--) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim trailing
    return move(s);
}

String repeat(const string& s, uint times) {
    String r (times*s.size); for(uint unused i: range(times)) r<<s; return r;
}

String pad(const string& s, uint length, const string& pad) { return repeat(pad, max<int>(0,length-s.size/pad.size))+s; }

stringz strz(const string& s) { stringz r; r.reserve(s.size+1); r << s; r << '\0'; return r; }

/// array<string>

array<string> split(const string& str, byte separator) {
    array<string> list;
    const byte* b=str.begin();
    const byte* end=str.end();
    for(;;) {
        const byte* e = b;
        while(e!=end && *e!=separator) ++e;
        if(b==end) break;
        list << string(b,e-b);
        if(e==end) break;
        b = e;
        if(b!=end && *b==separator) ++b;
    }
    return list;
}

/// Number conversions

template<uint base> String utoa(uint64 n, int pad) {
    assert(base>=2 && base<=16);
    byte buf[64]; int i=64;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = '0';
    return String(string(buf+i,64-i));
}
template String utoa<2>(uint64,int);
template String utoa<16>(uint64,int);

template<uint base> String itoa(int64 number, int pad, char padChar=' ') {
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
template String itoa<10>(int64,int,char);

String ftoa(double n, int precision, int pad, int exponent, bool inf) {
    bool sign = n<0; n=abs(n);
    if(__builtin_isnan(n)) return String("NaN"_);
    if(n==::inf) { assert_(inf); return String("inf"_); } //"∞"_
    if(n==-::inf) { assert_(inf); return String("-inf"_); } //"-∞"_
    int e=0; if(n && exponent && (n<1 || log10(n)>=precision+4)) e=floor(log10(n) / exponent) * exponent, n /= exp10(e);
    String s;
    if(sign) s<<'-';
    if(precision && n!=round(n)) {
        double integer=1, fract=__builtin_modf(n, &integer);
        uint decimal = round(fract*exp10(precision));
        if(decimal==(uint)exp10(precision)) integer++, decimal=0; // Rounds to ceiling integer
        s<<utoa(integer,pad)<<'.'<< utoa<10>(decimal,precision);
    } else s<<utoa(round(n));
    if(exponent==3 && e==3) s<<'K';
    else if(exponent==3 && e==6) s<<'M';
    else if(exponent==3 && e==9) s<<'G';
    else if(e) s<<'e'<<itoa<10>(e);
    return move(s);
}
