#include "string.h"
#include "array.cc"

/// ref<byte>

bool operator >(const ref<byte>& a, const ref<byte>& b) {
    for(uint i=0;i<min(a.size,b.size);i++) {
        if(a[i] < b[i]) return false;
        if(a[i] > b[i]) return true;
    }
    return a.size > b.size;
}

ref<byte> str(const char* s) { if(!s) return "null"_; int i=0; while(s[i]) i++; return ref<byte>((byte*)s,i); }

bool startsWith(const ref<byte>& s, const ref<byte>& a) { return a.size<=s.size && ref<byte>(s.data,a.size)==a; }
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

ref<byte> section(const ref<byte>& s, byte separator, int start, int end, bool includeSeparator) {
    if(!s) return ""_;
    uint b,e;
    if(start>=0) {
        b=0;
        auto it=s.begin(); for(uint i=0;i<(uint)start && it!=s.end();++it,b++) if(*it==separator) i++;
    } else {
        b=s.size;
        if(start!=-1) {
            auto it=s.end(); --it; --b;
            for(uint i=0;;--it,--b) {
                if(*it==separator) { i++; if(i>=uint(-start-1)) { if(!includeSeparator) b++; break; } }
                if(it == s.begin()) break;
            }
        }
    }
    if(end>=0) {
        e=0;
        auto it=s.begin();
        for(uint i=0;it!=s.end();++it,e++) if(*it==separator) { i++; if(i>=(uint)end) { if(includeSeparator) e++; break; } }
    } else {
        e=s.size;
        if(end!=-1) {
            auto it=s.end(); --it; --e;
            for(uint i=0;;--it,--e) {
                if(*it==separator) { i++; if(i>=uint(-end-1)) { if(includeSeparator) e++; break; } }
                if(it == s.begin()) break;
            }
        }
    }
    assert(e>=b,"'"_+s+"'"_,separator,start,end,includeSeparator,e,b);
    return ref<byte>(s.data+b,e-b);
}

ref<byte> trim(const ref<byte>& s) {
    int begin=0,end=s.size;
    for(;begin<end;begin++) { byte c=s[begin]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
    for(;end>begin;end--) { uint c=s[end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
    return slice(s, begin, end-begin);
}

bool isInteger(const ref<byte>& s) { if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }

long toInteger(const ref<byte>& number, int base) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    int sign=1;
    const byte* i = number.begin();
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    long value=0;
    for(;i!=number.end();++i) {
        if(*i==' ') break;
        int n;
        if(*i>='0' && *i<='9') n = *i-'0';
        else if(*i>='a' && *i<='f') n = *i+10-'a';
        else if(*i>='A' && *i<='F') n = *i+10-'A';
        else error("Invalid input '"_+number+"'"_);
        value *= base;
        value += n;
    }
    return sign*value;
}

/// string

array< ref<byte> > split(const ref<byte>& str, byte sep) {
    array< ref<byte> > list;
    auto b=str.begin();
    auto end=str.end();
    for(;;) {
        while(b!=end && *b==sep) ++b;
        auto e = b;
        while(e!=end && *e!=sep) ++e;
        if(b==end) break;
        list << ref<byte>(b,e);
        if(e==end) break;
        b = ++e;
    }
    return list;
}

string join(const ref<string>& list, const ref<byte>& separator) {
    string str;
    for(uint i=0;i<list.size;i++) { str<< list[i]; if(i<list.size-1) str<<separator; }
    return str;
}

string replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after) {
    string r(s.size);
    for(uint i=0;i<s.size;) {
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

//use O(N) removeAt instead of copying to avoid reallocation on simple strings, also this allow inplace reallocation
string simplify(string&& s) {
    uint i=0;
    while(i<s.size()) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim heading //TODO: no copy
    for(;i<s.size();) {
        byte c=s[i];
        if(c=='\r') { s.removeAt(i); continue; } //Removes any \r
        i++;
        if(c==' '||c=='\t'||c=='\n') while(i<s.size()) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //Simplify whitespace
    }
    for(i--;i>0;i--) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; s.removeAt(i); } //trim trailing
    return move(s);
}

stringz strz(const ref<byte>& s) { stringz r; r.reserve(s.size); r<<s<<0; return r; }

template<int base> string utoa(uint n, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    byte buf[32]; int i=32;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(32-i<pad) buf[--i] = '0';
    return string(ref<byte>(buf+i,32-i));
}
template string utoa<2>(uint,int);
template string utoa<16>(uint,int);

/// Integer conversions

template<int base> string itoa(int number, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    byte buf[32]; int i=32;
    uint n=abs(number);
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(32-i<pad) buf[--i] = '0';
    if(number<0) buf[--i]='-';
    return string(ref<byte>(buf+i,32-i));
}
template string itoa<10>(int,int);
