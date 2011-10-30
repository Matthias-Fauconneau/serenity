#pragma once
#include <functional>

using namespace std::placeholders;
using std::move;
using std::forward;

extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

#define can_forward_(F,T) std::is_convertible<typename std::remove_reference<F>::type, typename std::remove_reference<T>::type>::value
#define can_forward(T) can_forward_(T##f,T)
#define perfect_(F,T) template<class F, predicate(can_forward_(F,T)) >
#define perfect(T) perfect_(T##f,T)
#define perfect2_(F,T,G,U) template<class F, class G, predicate(can_forward_(F,T)), predicate1(can_forward_(G,U)) >
#define perfect2(T,U) perfect2_(T##f,T,U##f,U)

/// types

typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef long int64;
typedef unsigned long uint64;
typedef unsigned long size_t;
typedef long ssize_t;
template <class V, class T, int N> struct vector;
template <class T> struct array;
typedef array<uint8> raw;
typedef array<char> string;
#define _(s) string(s,sizeof(s)-1)
template <class K, class V> struct map;

#define DefaultConstructor(object) object()=default
#define MoveOnly(o) o(o&)=delete; o(o&&)=default; o& operator=(const o&)=delete; o& operator=(o&&)=default

#define signal(Args...) array<std::function<void(Args)>>
#define connect(signal, slot, args...) signal << std::bind(&std::remove_reference<decltype(*this)>::type::slot, this , ## args);
#define emit(signal, args...) ({ for(auto slot: signal) slot(args); })

extern "C" {
void abort() throw() __attribute((noreturn));
void* malloc(size_t size) throw();
void* realloc(void* ptr, size_t size) throw();
void free(void *ptr) throw();
void *memcpy(void*__restrict dst, const void*__restrict src, size_t n) throw();
void *memmove(void *dest, const void *src, size_t n) throw();
void *memset (void* s, int c, size_t n) throw();
ssize_t write(int fd, const void* buf, size_t n);
inline size_t strlen(const char* s) throw() { size_t i=0; while(s[i]) i++; return i; }
}

/// algorithms

template <class T> T abs(T x) { return x>=0 ? x : -x; }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template <class T> T sqr(T x) { return x*x; }
template <class T> void copy(T* dst,const T* src, int count) { memcpy(dst,src,(size_t)count*sizeof(T)); }
template <class T> void clear(T& dst) { memset(&dst,0,sizeof(T)); }
template <class T> void clear(T* dst, int count) { memset(dst,0,(size_t)count*sizeof(T)); }
template <class T> T copy(const T& t) { return t; }
template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template <class T> void reverse(T& a) { for(int i=0; i<a.size/2; i++) swap(a[i], a[a.size-i-1]); }
template <class T, predicate(sizeof(T)==4)> constexpr uint32 swap(T x) {
	return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24);
}
template <class T, predicate(sizeof(T)==2)> constexpr uint16 swap(T x) { return ((x>>8)&0xff)|((x&0xff)<<8); }
template <class T, predicate(sizeof(T)==1)> constexpr uint8 swap(T t) { return t; }

/// debug
inline void log_(bool b) { b?write(1,"true",sizeof("true")-1):write(1,"false",sizeof("false")-1); }
inline void log_(char c) { write(1, &c, 1); }
inline void log_(const char* s) { write(1, s, (size_t)strlen(s)); }

void log_(int64);
void log_(uint64);
void log_(int32);
void log_(uint32);
void log_(int16);
void log_(uint16);
void log_(int8);
void log_(uint8);
void log_(double);
void log_(float);
void log_(const string& s);

template <class V, class T, int N> inline void log_(const vector<V,T,N>& v) {
	log_('(');
	for(int i=0;i<N;i++) { log_(v[i]); if(i<N-1) log_(_(", ")); }
	log_(')');
}
template<class T> void log_(const array<T>& a) {
	log_('[');
	for(int i=0;i<a.size;i++) { log_(a[i]); if(i<a.size-1) log_(_(", ")); }
	log_(']');
}
template<class K, class V> void log_(const map<K,V>& m) {
	log_('{');
	for(int i=0;i<m.size();i++) { log_(m.keys[i]); log_(_(": ")); log_(m.values[i]); if(i<m.size()-1) log_(_(", ")); }
	log_('}');
}
template<class A, class... Args> void log_(const A& a, const Args&... args) { log_(a); log_(' '); log_(args...); }
template<class... Args> void log(const Args&... args) { log_(args...); log_('\n'); }

void logBacktrace();
#define fail(args...) ({ logBacktrace(); log_("Critical Failure:\t"); log(args); abort(); })
#undef assert
#define assert(expr, args...) ({ if(!(expr)) { logBacktrace(); log_("Assertion Failure:\t"); log(#expr, ## args); abort(); } })
