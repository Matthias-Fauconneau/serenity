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

bool endsWith(const string s, const string a) {
    return a.size<=s.size && string(s.data+s.size-a.size,a.size)==a;
}

bool find(const string s, const string a) {
    if(a.size>s.size) return false;
    for(uint i=0;i<=s.size-a.size;i++) if(string(s.data+i,a.size)==a) return true;
    return false;
}

string section(const string s, byte separator, int begin, int end) {
	if(!s) return ""_;
    uint b,e;
    if(begin>=0) { b=0; for(uint i=0;i<(uint)begin && b<s.size;b++) if(s[b]==separator) i++; }
    else { b=s.size; if(begin!=-1) { b--; for(uint i=0;b>0;b--) if(s[b]==separator) { i++; if(i>=uint(-begin-1)) { b++; break; } } } }
    if(end>=0) { e=0; for(uint i=0;e<s.size;e++) if(s[e]==separator) { i++; if(i>=(uint)end) break; } }
    else { e=s.size; if(end!=-1) { e--; for(uint i=0;e>0;e--) { if(s[e]==separator) { i++; if(i>=uint(-end-1)) break; } } } }
    assert(e>=b,s,separator,begin,end);
    return string(s.data+b,e-b);
}

string trim(const string s) {
	int begin=0,end=s.size;
	for(;begin<end;begin++) { byte c=s[(uint)begin]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
	for(;end>begin;end--) { uint c=s[(uint)end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
	return s.slice(begin, end-begin);
}

bool isInteger(const string s) { if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }
int64 parseInteger(const string number, int base) { return TextData(number).integer(true, base); }
bool isDecimal(const string s) { return TextData(s).whileDecimal().size == s.size; }
double parseDecimal(const string number) { return TextData(number).decimal(); }

// -- String

char lowerCase(char c) { return c>='A'&&c<='Z'?'a'+c-'A':c; }
String toLower(string source) { return apply(source, lowerCase); }

char upperCase(char c) { return c>='a'&&c<='z'?'A'+c-'a':c; }
String toUpper(string source) { return apply(source, upperCase); }

String repeat(string s, uint times) { array<char> r (times*s.size, 0); for(uint unused i: range(times)) r.append(s); return move(r); }

String left(const string source, size_t size, const char pad) {
	if(source.size >= size) return copyRef(source);
	String target(size, 0);
    target.append(source);
    while(target.size < size) target.append(pad);
    return target;
}
String right(const string source, size_t size, const char pad) {
	if(source.size >= size) return copyRef(source);
	String target(size, 0);
	while(target.size < size-source.size) target.append(pad);
    target.append(source);
    return target;
}

String replace(string s, string before, string after) {
	array<char> r(s.size, 0);
	for(size_t i=0; i<s.size;) {
		if(i<=s.size-before.size && string(s.data+i, before.size)==before) { r.append(after); i+=before.size; }
		else { r.append( s[i] ); i++; }
	}
	return move(r);
}

String simplify(array<char>&& s) {
	for(size_t i=0; i<s.size;) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim heading
	for(size_t i=0; i<s.size;) {
		byte c=s[i];
		if(c=='\r') { s.removeAt(i); continue; } //Removes any \r
		i++;
	   if(c==' '||c=='\t'||c=='\n') while(i<s.size) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //Simplify whitespace
	}
	if(s.size) for(size_t i=s.size-1;i>0;i--) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim trailing
	return move(s);
}

// -- string[]

String join(ref<string> list, const string separator) {
    if(!list) return {};
    size_t size = 0; for(auto e: list) size += e.size;
	String target ( size + (list.size-1)*separator.size, 0);
    for(size_t i: range(list.size)) { target.append( list[i] ); if(i<list.size-1) target.append( separator ); }
    return target;
}

buffer<string> split(const string source, string separator) {
    array<string> list;
    TextData s (source);
    while(s) list.append( s.until(separator) );
    return move(list);
}

// -- Number conversions

String str(uint64 n, uint pad, char padChar, uint base) {
    assert(base>=2 && base<=16);
	byte buf[64]; uint i=64;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(64-i<pad) buf[--i] = padChar;
	return copyRef(string(buf+i,64-i));
}

String str(int64 number, uint pad, char padChar, uint base) {
    assert(base>=2 && base<=16);
	byte buf[64]; uint i=64;
    uint64 n=abs(number);
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    if(number<0) buf[--i]='-';
#if __GNUC__
	assert_(i<64); // Fixes wrong 'array subscript is above array bounds' warning given by GCC
#endif
	while(64<pad+i) buf[--i] = padChar;
	return copyRef(string(buf+i,64-i));
}

String str(double n, uint precision, uint exponent, uint pad) {
    bool sign = n<0; n=abs(n);
    if(__builtin_isnan(n)) return ::right("NaN", pad);
    if(n==::inf) return ::right("∞", pad+2);
    if(n==-::inf) return ::right("-∞", pad+2);
    int e=0; if(n && exponent && (n<1 || log10(n)>=precision+4)) e=floor(log10(n) / exponent) * exponent, n /= exp10(e);
    array<char> s;
    if(sign) s.append('-');
    if(precision /*&& n!=round(n)*/) {
        double integer=1, fract=__builtin_modf(n, &integer);
        uint64 decimal = round(fract*exp10(precision));
        uint exp10=1; for(uint i unused: range(precision)) exp10*=10; // Integer exp10(precision)
        if(decimal==exp10) integer++, decimal=0; // Rounds to ceiling integer
        assert_(isNumber(integer)/*, integer, n*/);
        s.append( str(uint64(integer)) );
        s.append('.');
		s.append( str(decimal, precision) );
    } else s.append( str(uint64(round(n))) );
    if(exponent==3 && e==3) s.append('K');
    else if(exponent==3 && e==6) s.append('M');
    else if(exponent==3 && e==9) s.append('G');
    else if(e) { s.append('e'); s.append(str(e)); }
	if(pad > s.size) return right(s, pad);
	return move(s);
}

String binaryPrefix(uint64 value, string unit, string unitSuffix) {
    if(value < 1u<<10) return str(value, unit);
    if(value < 10u<<20) return str(value/1024.0,"ki"_+(unitSuffix?:unit));
    if(value < 10u<<30) return str(value/1024.0/1024.0,"Mi"_+(unitSuffix?:unit));
    return str(value/1024.0/1024.0/1024.0,"Gi"_+(unitSuffix?:unit));
}
String decimalPrefix(double value, string unit, string unitSuffix) {
    if(value < 1) {
     if(value > 1e-3) return str(int(value*1e3))+"m"_+(unitSuffix?:unit);
     return str(int(value*1e6))+"u"_+(unitSuffix?:unit);
    }
    if(value < 1e3) return str(int(value))+unit;
    if(value < 1e6) return str(int(value/1e3))+"K"_+(unitSuffix?:unit);
    if(value < 1e9) return str(int(value/1e6))+"M"_+(unitSuffix?:unit);
    return str(int(value/1e9))+"G"_+(unitSuffix?:unit);
}
