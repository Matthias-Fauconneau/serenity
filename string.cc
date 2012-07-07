#include "string.h"

#include "array.cc"
Array_Copy_Compare_Sort_Default(string)

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
        if((code&0b11000000)!=0b10000000) {} //Windows-1252
        else { //UTF-8
            int i=0; for(;(code&0b11000000)==0b10000000;i++) code = *(--pointer);
            if(i==1) { if((code&0b11100000)!=0b11000000) pointer++; }
            else if(i==2) { if((code&0b11110000)!=0b11100000) pointer+=2; }
            else if(i==3) { if((code&0b11111000)!=0b11110000) pointer+=3; }
            else if(i==4) { if((code&0b11111100)!=0b11111000) pointer+=4; }
            else if(i==5) { if((code&0b11111110)!=0b11111100) pointer+=5; }
            else error(i);
        }
    }
    return *this;
}

uint string::at(uint index) const {
    utf8_iterator it=begin();
    for(uint i=0;it!=end();++it,++i) if(i==index) return *it;
    error("Invalid UTF8");
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
bool endsWith(const array<byte>& s, const array<byte>& a) {
    return a.size()<=s.size() && string(s.data()+s.size()-a.size(),a.size())==a;
}

bool operator >(const array<byte>& a, const array<byte>& b) {
    for(uint i=0;i<min(a.size(),b.size());i++) {
        if(a[i] < b[i]) return false;
        if(a[i] > b[i]) return true;
    }
    return a.size() > b.size();
}

bool CString::busy = 0; //flag for multiple strz usage in a statement
CString strz(const string& s) {
    CString cstr;
    //optimization to avoid copy, valid so long the referenced string doesn't change
    if(s.capacity()>s.size()) { ((byte*)s.data())[s.size()]=0; cstr.tag=0; cstr.data=(char*)s.data(); return cstr; }
    //optimization to avoid allocation, dangling after ~CString (on statement end) until any reuse
    if(!CString::busy) { CString::busy=1; static char buffer[256]; cstr.tag=1; cstr.data=buffer; }
    //temporary allocation, dangling after ~CString (on statement end) until any malloc
    else error("heap strz"); //{ cstr.tag=2; cstr.data=allocate<char>(s.size()+1); }
    copy(cstr.data,(char*)s.data(),s.size()); cstr.data[s.size()]=0;
    return cstr;
}
string str(const char* s) { if(!s) return "null"_; int i=0; while(s[i]) i++; return string((byte*)s,i); }
string strz(const char* s) { return copy(str(s)); }

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
            for(uint i=0;;--it,--b) {
                if(*it==separator) { i++; if(i>=uint(-start-1)) { if(!includeSeparator) b++; break; } }
                if(it == s.begin()) break;
            }
        }
    }
    if(end>=0) {
        e=0;
        utf8_iterator it=s.begin();
        for(uint i=0;it!=s.end();++it,e++) if(*it==separator) { i++; if(i>=(uint)end) { if(includeSeparator) e++; break; } }
    } else {
        e=s.size();
        if(end!=-1) {
            utf8_iterator it=s.end(); --it; --e;
            for(uint i=0;;--it,--e) {
                if(*it==separator) { i++; if(i>=uint(-end-1)) { if(includeSeparator) e++; break; } }
                if(it == s.begin()) break;
            }
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

array<byte> replace(const array<byte>& s, const array<byte>& before, const array<byte>& after) {
    array<byte> r(s.size());
    for(uint i=0;i<s.size();) {
        if(i<=s.size()-before.size() && string(s.data()+i, before.size())==before) { r<<after; i+=before.size(); }
        else { r << s[i]; i++; }
    }
    return r;
}

string toLower(const string& s) {
    string lower;
    for(char c: s) if(c>='A'&&c<='Z') lower<<'a'+c-'A'; else lower << c;
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
    /**/  if(c<(1<<7)) utf8 << c;
    else if(c<(1<<(7+6))) utf8 << (0b11000000|(c>>6)) << (0b10000000|(c&0b111111));
    else if(c<(1<<(7+6+6))) utf8 << (0b11100000|(c>>12)) << (0b10000000|((c>>6)&0b111111)) << (0b10000000|(c&0b111111));
    else error(c);
    return utf8;
}

/// Human-readable value representation

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

bool isInteger(const string& s) { if(!s) return false; for(char c: s) if(c<'0'||c>'9') return false; return true; }

long toInteger(const string& number, int base) {
    assert(base>=2 && base<=16,"Unsupported base"_,base);
    int sign=1;
    const byte* i = number.begin().pointer;
    if(*i == '-' ) ++i, sign=-1; else if(*i == '+') ++i;
    long value=0;
    for(;i!=number.end().pointer;++i) {
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
