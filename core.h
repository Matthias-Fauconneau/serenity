#pragma once

/// workaround missing IDE support
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define for()
#define constexpr
#define override
#define _
#endif

/// primitive types

typedef signed char int8;
typedef char byte;
typedef unsigned char uint8;
typedef unsigned char ubyte;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef long int64;
typedef unsigned long uint64;
typedef unsigned long size_t;
typedef long ssize_t;

/// language support

#define declare(function, attributes...) function __attribute((attributes)); function
#define no_trace(function) function __attribute((no_instrument_function)); function
no_trace(inline void* operator new(uint64, void* p)) { return p; }
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

#include <type_traits>
template<typename T> constexpr typename std::remove_reference<T>::type&& move(T&& t){ return (typename std::remove_reference<T>::type&&)t; }
template<typename T> constexpr T&& forward(typename std::remove_reference<T>::type& t) { return (T&&)t; }
template<typename T> constexpr T&& forward(typename std::remove_reference<T>::type&& t) {
	static_assert(!std::is_lvalue_reference<T>::value,"forwarding an lvalue"); return (T&&)t;
}
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;
#define move_only(o) no_copy(o) o(o&&)=default; o& operator=(o&&)=default;
#define no_move(o) no_copy(o) o(o&&)=delete; o& operator=(o&&)=delete;
#define is_convertible(F,T) std::is_convertible<F,T>::value
#define is_same(F,T) std::is_same<F,T>::value
#define can_forward_(F,T) is_convertible(typename std::remove_reference<F>::type, typename std::remove_reference<T>::type)
#define can_forward(T) can_forward_(T##f,T)
#define perfect_(F,T) template<class F, predicate(can_forward_(F,T)) >
#define perfect(T) perfect_(T##f,T)
#define perfect2_(F,T,G,U) template<class F, class G, predicate(can_forward_(F,T)), predicate1(can_forward_(G,U)) >
#define perfect2(T,U) perfect2_(T##f,T,U##f,U)

/*struct unroll { template<typename ...T> unroll(T...) {} };
#define unroll(call) ({ unroll{(call, 1)...}; })*/

template<typename... Args> struct delegate {
	void* _this;
	void (*method)(void*, Args...);
	template <class C> delegate(C* _this, void (C::*method)(Args...)) : _this((void*)_this), method((void(*)(void*, Args...))method) {}
};
template<class T> struct array;
template<typename... Args> struct signal : array< delegate<Args...> > {
	void emit(Args... args) { for(auto slot: *this) slot.method(slot._this, args...);  }
	template <class C> void connect(C* _this, void (C::*method)(Args...)) {
		*this << delegate<Args...>(_this, method);
	}
};

/// memory allocation

extern "C" {
void* malloc(size_t size) throw();
void* realloc(void* ptr, size_t size) throw();
void free(void *ptr) throw();
}

/// algorithms

template<class T> T* addressof( T & v ) { return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char &>(v))); }

//TODO: SSE
template <class T> void set(T* data, int count, T value) { auto d=(byte*)data; for(uint i=0;i<count*sizeof(T);i++) d[i]=value; }
template <class T> void clear(T* data, int count) { auto d=(byte*)data; for(uint i=0;i<count*sizeof(T);i++) d[i]=0; }
template <class T> void clear(T& data) { clear(&data,1); }
template <class T> void copy(T* dst,const T* src, int count) { auto d=(byte*)dst,s=(byte*)src; for(uint i=0;i<count*sizeof(T);i++) d[i]=s[i]; }
template <class T> void copy(T& dst,const T& src) { copy(addressof(dst),addressof(src),1); }
template <class T> T copy(const T& t) { return t; }
template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }

template <class T> T sqr(T x) { return x*x; }
template <class T> T abs(T x) { return x>=0 ? x : -x; }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }

inline int padding(int offset, int align) { return (-offset) & (align - 1); }
inline int align(int offset, int align) { return (offset + align - 1) & ~(align - 1); }

/// debugging support

struct string;
string operator "" _(const char* data, size_t size);

template<class A> void log_(const A&) { static_assert(sizeof(A) && 0,"Unknown format"); }
template<class A, class... Args> void log_(const A& a, const Args&... args) { log_(a); log_(' '); log_(args...); }
template<class... Args> void log(const Args&... args) { log_(args...); log_('\n'); }

template<> void log_(const string&);
template<> void log_(const bool&);
template<> void log_(const char&);
template<> void log_(const int8&);
template<> void log_(const uint8&);
template<> void log_(const int16&);
template<> void log_(const uint16&);
template<> void log_(const int32&);
template<> void log_(const uint32&);
template<> void log_(const int64&);
template<> void log_(const uint64&);
template<> void log_(const float&);
template<> void log_(const double&);
//template<class A> void log_(const A* a) { if(a) log_(*a); else log_("null"_); }
template<> void log_(char* const&);

#ifdef TRACE
extern bool trace_enable;
#define trace_on  trace_enable=true;
#define trace_off trace_enable=false;
#else
#define trace_on
#define trace_off
#endif

#ifdef DEBUG
void logFrame();
void logTrace();
extern "C" void abort() throw() __attribute((noreturn));
#define fail() ({ trace_off; logTrace(); abort(); })
#define error(args...) ({ trace_off; logTrace(); log("Error:\t"_,##args); abort(); })
#define assert(expr, args...) ({ if(!(expr)) { trace_off; logTrace(); log("Assert:\t"_,#expr##_, ##args); abort(); } })
#else
#define fail() ({})
#define error(args...) ({})
#define assert(expr, args...) ({})
#endif

/// import runtime support classes
#include "array.h"
#include "string.h"
