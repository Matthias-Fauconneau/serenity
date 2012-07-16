#include "string.h"
#include "array.cc"
//Array_Copy_Compare_Sort_Default(string)

/// string operations

bool startsWith(const ref<byte> &s, const ref<byte> &a) { return a.size<=s.size && ref<byte>(s.data,a.size)==a; }
bool find(const ref<byte>& s, const ref<byte>& a) {
    if(a.size>s.size) return false;
    for(uint i=0;i<=s.size-a.size;i++) {
        if(string(s.data+i,a.size)==a) return true;
    }
    return false;
}
bool endsWith(const ref<byte>& s, const ref<byte>& a) {
    return a.size<=s.size && string(s.data+s.size-a.size,a.size)==a;
}

bool operator >(const ref<byte>& a, const ref<byte>& b) {
    for(uint i=0;i<min(a.size,b.size);i++) {
        if(a[i] < b[i]) return false;
        if(a[i] > b[i]) return true;
    }
    return a.size > b.size;
}

bool CString::busy = 0; //flag for multiple strz usage in a statement
CString strz(const ref<byte> &s) {
    CString cstr;
    //optimization to avoid copy, valid so long the referenced string doesn't change
    //if(s.capacity()>s.size) { ((byte*)s.data)[s.size]=0; cstr.tag=0; cstr.data=(char*)s.data; return cstr; }
    //optimization to avoid allocation, dangling after ~CString (on statement end) until any reuse
    if(!CString::busy) { CString::busy=1; static char buffer[256]; cstr.tag=1; cstr.data=buffer; }
    //temporary allocation, dangling after ~CString (on statement end) until any malloc
    else error("heap strz"); //{ cstr.tag=2; cstr.data=allocate<char>(s.size+1); }
    copy(cstr.data,(char*)s.data,s.size); cstr.data[s.size]=0;
    return cstr;
}
string str(const char* s) { if(!s) return string("null"_); int i=0; while(s[i]) i++; return string((byte*)s,i); }

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

array<string> split(const ref<byte>& str, byte sep) {
    array<string> list;
    auto b=str.begin();
    auto end=str.end();
    for(;;) {
        while(b!=end && *b==sep) ++b;
        auto e = b;
        while(e!=end && *e!=sep) ++e;
        if(b==end) break;
        list << copy(string(b,e));
        if(e==end) break;
        b = ++e;
    }
    return list;
}

string join(const array<string>& list, const ref<byte>& separator) {
    string str;
    for(uint i=0;i<list.size();i++) { str<<list[i]; if(i<list.size()-1) str<<separator; }
    return str;
}

array<byte> replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after) {
    array<byte> r(s.size);
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

ref<byte> trim(const ref<byte>& s) {
    int i=0,end=s.size;
    for(;i<end;i++) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim heading
    for(;end>i;end--) { uint c=s[end-1]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; } //trim trailing
    return slice(s, i, end-i);
}

string simplify(const ref<byte>& s) {
    string simple;
    for(int i=0,end=s.size;i<end;) { //trim duplicate
        byte c=s[i];
        if(c=='\r') { i++; continue; }
        simple << c;
        i++;
        if(c==' '||c=='\t'||c=='\n') for(;i<end;i++) { byte c=s[i]; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') break; }
    }
    return simple;
}

template<int base> string utoa(uint n, int pad) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    byte buf[32]; int i=32;
    do {
        buf[--i] = "0123456789abcdef"[n%base];
        n /= base;
    } while( n!=0 );
    while(32-i<pad) buf[--i] = '0';
    return copy(string(buf+i,32-i));
}
template string utoa<16>(uint,int);

/// Conversions between number <-> string

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
    return copy(string(buf+i,32-i));
}
template string itoa<2>(int,int);
template string itoa<10>(int,int);

/*string ftoa(float n, int precision, int base) {
    if(__builtin_isnan(n)) return "NaN"_;
    if(n==__builtin_inff()) return "∞"_;
    if(n==-__builtin_inff()) return "-∞"_;
    int m=1; for(int i=0;i<precision;i++) m*=base;
    return (n>=0?""_:"-"_)+utoa<10>(abs(n))+"."_+utoa<10>(uint(m*abs(n))%m,precision);
}*/

bool isInteger(const ref<byte>& s) { if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }

long toInteger(const ref<byte> &number, int base) {
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
