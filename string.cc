#include "string.h"

/// string

string toString(long number, int base, int pad) {
	assert(base>=2 && base<=16,"Unsupported base",base);
	char buf[20]; int i=19;
	long n = abs(number);
	if(n >= 0x10000) { base=16; pad=8; }
    do {
        buf[--i] = "0123456789ABCDEF"[n%base];
        n /= base;
    } while( n!=0 );
	while(19-i<pad) buf[--i] = '0';
    if(number<0) buf[--i] = '-';
    if(base==16) { buf[--i] = 'x'; buf[--i] = '0'; }
	string s(buf+i,19-i); s.detach(); return s;
}
string toString(double number) {
    return _(".")+toString(long(100*number));
}
long readInteger(const char*& s, int base) {
	assert(base>=2 && base<=16,"Unsupported base",base);
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
long toInteger(const string& str, int base) { const char* s=str.data; return readInteger(s,base); }
double readFloat(const char*& s, int base ) {
    assert(base>2 && base<16,"Unsupported base",base);
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
double readFloat(const string& str, int base ) { const char* s=str.data; return readFloat(s,base); }

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
string strz(const string& s) { return s+_("\0"); }
string strz(const char* s) { int i=0; while(s[i]) i++; return string(s,i); }

string trim(const string& s) {
	int b=0; for(;b<s.size && s[b]==' ';) b++;
	int e=s.size-1; for(;e>b && s[e]==' ';) e--;
	return s.slice(b,e+1-b);
}

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
void log_(bool b) { log_(b?_("true"):_("false")); }
void log_(char c) { log_(string(&c,1)); }
void log_(int8 n) { log_((int64)n); }
void log_(uint8 n) { log_((int64)n); }
void log_(int16 n) { log_((int64)n); }
void log_(uint16 n) { log_((int64)n); }
void log_(int32 n) { log_((int64)n); }
void log_(uint32 n) { log_((int64)n); }
void log_(int64 n) { log_(toString(n)); }
void log_(uint64 n) { log_((int64)n); }
void log_(void* n) { log_(toString((long)n,16,8)); }
void log_(float n) { log_((double)n); }
void log_(double n) { log_(toString(n)); }
void log_(const char* s) { log_(strz(s)); }
void log_(const string& s) { write(1,s.data,(size_t)s.size); }

/// stream

long TextStream::readInteger(int base) { auto b=(const char*)&data[i], e=b; long r = ::readInteger(e,base); i+=int(e-b); return r; }
double TextStream::readFloat(int base) { auto b=(const char*)&data[i], e=b; double r = ::readFloat(e,base); i+=int(e-b); return r; }
