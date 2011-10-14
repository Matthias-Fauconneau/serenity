#pragma once

// number
typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef int int32;
template <typename T> T abs(T x) { return x < 0 ? -x : x; }
template <typename T> T min(T a, T b) { return a < b ? a : b; }
template <typename T> T max(T a, T b) { return a > b ? a : b; }
template <typename T> T sqr(T x) { return x*x; }
#define swap32(x) ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define swap16(x) ((unsigned short int) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8)))
// memory
#include <string.h> //memset, memcpy, strlen
template <typename T> void copy(T* dst,const T* src, int count) { memcpy(dst,src,sizeof(T)*count); }
template <typename T> void clear(T* dst,int count) { memset(dst,0,sizeof(T)*count); }

// debug
void log_() {}
void log_(const char* s) { write(1, s, strlen(s)); }
void log_(long n);
void log_(ulong n);
void log_(int n);
void log_(uint n);
void log_(double n);
void log_(float n);
template<typename A, typename B, typename... Args>
void log_(const A& a, const B& b, const Args&... args) { log_(a); write(1," ",1); log_(b); write(1," ",1); log_(args...); }
template<typename... Args> void log(const Args&... args) { log_(args...); write(1,"\n",1); }
#define assert(expr, args... ) ({ if(!(expr)) { log(#expr,args); abort(); } })

// array
template <typename T> struct array {
    const T* data = 0;
    int size = 0;
    int capacity = 0; //0 = not owned

    array() = default;
    array(array& o) = delete;
    array& operator=(const array&) = delete;
    array(array&& o) : data(o.data), size(o.size), capacity(o.capacity) {}
    array(int capacity) : data(new T[capacity]), capacity(capacity) {}
    array(const T* data, int size) : data(data), size(size) {}
    array(const T* begin,const T* end) : data(begin), size(end-begin) {}
    ~array() { if(capacity) delete data; }
    void reserve(int s) { if(capacity>=s) return; T* copy=new T[capacity=s]; ::copy(copy,data,size); data=copy; } //TODO: realloc, POT
    void resize(int s) { reserve(s); size=s; }
    void detach() { reserve(size); }
    void clear() { size=0; }

    const T& at(int i) const { assert(i>=0 && i<size,"Bound check",i,size); return data[i]; }
    const T& operator [](int i) const { return at(i); }
    const T& first() const { return data[0]; }
    const T& last() const { return data[size-1]; }

    T& operator [](int i) { detach(); return (T&)at(i); }
    T& first() { detach(); return ((T*)data)[0]; }
    T& last() { detach(); return ((T*)data)[size-1]; }

    void copy(T* dst) const { ::copy(dst,data,size); }
    int indexOf(const T& v) const { for(int i=0;i<size;i++) { T e=data[i]; if( e==v ) return i; } return -1; }

    array<T> replace(T before, T after) const {
        array<T> r; r.resize(size); T* d=(T*)r.data; for(int i=0;i<size;i++) d[i] = data[i] == before ? after : data[i]; return r;
    }
    void removeAt(int i /*,int count=1*/) { detach(); size--; for(int j=i;j<size;j++) ((T*)data)[j]=data[j+1];}
    bool removeOne(T v) { int i=indexOf(v); if(i<0) return false; removeAt(i); return true; }
    array<T>& operator <<(T v) { int s=size+1; reserve(s); ((T*)data)[size]=v; size=s; return *this; }

    struct const_iterator {
        const T* t;
        const_iterator(const T* t) : t(t) {}
        bool operator!=(const const_iterator& o) const { return t != o.t; }
        const T& operator* () const { return *t; }
        const const_iterator& operator++ () { t++; return *this; }
    };
    const_iterator begin() const { return const_iterator(data); }
    const_iterator end() const { return const_iterator(&data[size]); }

    struct iterator {
        T* t;
        iterator(T* t) : t(t) {}
        bool operator!=(const iterator& o) const { return t != o.t; }
        T& operator* () const { return *t; }
        const iterator& operator++ () { t++; return *this; }
    };
    iterator begin() { return iterator((T*)data); }
    iterator end() { return iterator((T*)&data[size]); }
};

template <typename T> bool operator ==(const array<T>& a, const array<T>& b) {
    if(a.size != b.size) return false;
    for(int i=0;i<a.size;i++) if(a[i]!=b[i]) return false;
    return true;
}

template <typename A, typename T> struct cat {
    const A& a; const array<T>& b;
    struct { cat* c; operator int() const { return c->a.size+c->b.size; } } size; //property
    cat(const A& a,const array<T>& b) : a(a), b(b) { size.c=this; }
    void copy(T* data) const { a.copy(data); ::copy(data+a.size,b.data,b.size); }
    operator array<T>() { array<T> r; r.resize(size); copy((T*)r.data); return r; }
};
template <typename A, typename T> cat<A,T> operator +(const A& a,const array<T>& b) { return cat<A,T>(a,b); }

// string
typedef array<char> string;
bool operator ==(const string& a, const char* b) { return a==string(b,strlen(b)); }

string toString(long number, int base=10) {
    char buf[64]; int i=64;
    long n = abs(number);
    do {
        buf[--i] = "0123456789ABCDEF"[n%base];
        n /= base;
    } while( n!=0 );
    if(number<0) buf[--i] = '-';
    string s(buf+i,64-i); s.detach(); return s;
}
string toString(double number/*, int precision=2*/) {
    /*if( number!=number ) return string("nan",3);
    //TODO: if( n==double.infinity ) return "inf";
    int size = 64+1+precision;
    char buf[size]; int i=64;
    double f = abs(number);
    long n = f;
    do {
        buf[--i] = "0123456789ABCDEF"[n%10];
        n /= 10;
    } while( n!=0 );
    if(number<0) buf[--i] = '-';
    buf[64]='.';
    for(int i=65;i<size;i++) {
        f *= 10;
        buf[i++] = "0123456789"[int(f)%10];
    }
    string s(buf+i,size-i); s.detach(); return s;*/
    return string(".",1)+toString(long(100*number));
}
long toInteger(const string& str, int base=10 ) {
    int neg=0;
    const char* s = str.data;
    if(*s == '-' ) s++, neg=1; else if(*s == '+') s++;
    long number=0;
    for(;*s;s++) {
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if( n<0 ) break;
        number *= base;
        number += n;
    }
    return neg ? -number : number;
}

void log_(const string& s) { write(1,s.data,s.size); }
void log_(long n) { log_(toString(n)); }
void log_(ulong n) { log_((long)n); }
void log_(int n) { log_((long)n); }
void log_(uint n) { log_((long)n); }
void log_(double n) { log_(toString(n)); }
void log_(float n) { log_((double)n); }

string section(const char* str, char sep, int start=0, int end=1) {
    const char *b=0,*e=0;
    if(start>=0) {
        b=str;
        for(int i=0;i<start && *b;b++) if(*b==sep) i++;
    } else {
        b=str+strlen(str);
        if(start!=-1) for(int i=0;b>str;b--) { if(*b==sep) { i++; if(i>=-start-1) break; } }
        b++; //skip separator
    }
    if(end>=0) {
        e=str;
        for(int i=0;*e;e++) if(*e==sep) { i++; if(i>=end) break; }
    } else {
        e=str+strlen(str);
        if(end!=-1) for(int i=0;e>str;e--) { if(*e==sep) { i++; if(i>=-end-1) break; } }
    }
    return string(b,e);
}
bool endsWith(const char* s, const char* key) { int l=strlen(s),k=strlen(key); return l>=k && string(s+l-k,k)==string(key,k); }
bool match(const char*& src, const char* key) { for(const char* s = src;*s++==*key++;) if(!*key) { src=s; return true; } return false; }

// IO
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>

bool exists(const char* path) {
    int fd = open(path, O_RDONLY);
    if(fd >= 0) { close(fd); return true; }
    return false;
}

const void* mapFile(const char* path, int* size=0) {
    int fd = open(path, O_RDONLY);
    assert(fd >= 0,"File not found",path);
    struct stat sb; fstat(fd, &sb); if(size) *size=sb.st_size;
    const void* data = mmap(0,sb.st_size,PROT_READ,MAP_PRIVATE,fd,0); //|MAP_HUGETLB/*avoid seeks*/
    close(fd);
    return data;
}

template <typename T> void write(int fd, const T& t) { write(fd,&t,sizeof(T)); }

// time
#include <time.h>
int time() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }
