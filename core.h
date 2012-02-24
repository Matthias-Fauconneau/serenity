#pragma once

/// Missing C++11 IDE support workarounds
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define for()
#define constexpr
#define override
#define _
#define DEBUG
#define i( ignore )
#else
#define i( ignore... ) ignore
#endif

/// Language support

// Traits
#include <type_traits>
#define is_convertible(F,T) std::is_convertible<F,T>::value
#define is_same(F,T) std::is_same<F,T>::value
#define remove_reference(T) typename std::remove_reference<T>::type

// Predicates
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler
// perfect forwarding predicates
#define can_forward(T) is_convertible(remove_reference(T), remove_reference(T##f))
#define perfect(T) class T##f, predicate(can_forward(T))
#define perfect2(T,U) class T##f, class U##f, predicate(can_forward(T)), predicate1(can_forward(U))

// Move semantics
template<class T> inline constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)t; }
template<class T> inline constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<class T> inline constexpr T&& forward(remove_reference(T)&& t){static_assert(!std::is_lvalue_reference<T>::value,""); return (T&&)t; }
// delete copy constructors
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;

/// Primitive types

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

const float NaN = __builtin_nansf("");

/// Basic operations

template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template <class T> T abs(T x) { return x>=0 ? x : -x; }

/// Debugger

extern"C" ssize_t write(int fd, const void* buf, size_t size);
inline void log(const char* msg) { int i=0; while(msg[i]) i++; write(1,msg,(size_t)i); }
inline void log(char* msg) { log((const char*)msg); }

#ifdef DEBUG
/// compile \a statements in executable only if \a DEBUG flag is set
#define debug( statements... ) statements
#else
#define debug( statements... )
#endif

#ifdef NO_BFD
inline void logTrace() {}
#else
/// Log backtrace
void logTrace();
#endif

#ifdef TRACE
extern bool trace_enable;
#define trace_on trace_enable=true
#define trace_off trace_enable=false
#else
#define trace_on
#define trace_off
#endif

extern "C" void abort() throw() __attribute((noreturn));

/// Aborts the process without any message, stack trace is logged
#define fail() ({debug( trace_off; logTrace(); ) abort(); })

/// Aborts unconditionally
// can be used without string
#define error_(message) ({debug( trace_off; logTrace(); ) log("Error:\t"); log(#message); log("\n"); abort(); })

/// Aborts if \a expr evaluates to false
// can be used without string
#define assert_(expr) ({debug( if(!(expr)) { trace_off; logTrace(); log("Assert:\t"); log(#expr); log("\n"); abort(); } )})

/// Memory
inline void* operator new(uint64, void* p) { return p; } //placement new
extern "C" void* malloc(size_t size) throw();
extern "C" void* realloc(void* buffer, size_t size) throw();
extern "C" void free(void* buffer) throw();

#if TRACE_MALLOC
void* allocate_(int size, const char* type=0);
void* reallocate_(void* buffer, int oldsize, int size);
void unallocate_(void* buffer);
#include <typeinfo>
template<class T> T* allocate(int size) { return (T*)allocate_(size*sizeof(T), typeid(T).name()); }
template<class T> T* reallocate(const T* buffer, int oldSize, int size) { return (T*)reallocate_((void*)buffer,sizeof(T)*oldSize,sizeof(T)*size); }
template<class T> void unallocate(T* buffer) { unallocate_((void*)buffer); }
#else
// Allocates an uninitialized memory buffer for \a size elements
template<class T> T* allocate(int size) { return (T*)malloc(size*sizeof(T)); }
// Reallocates memory \a buffer to new \a size
template<class T> T* reallocate(const T* buffer, int, int size) { return (T*)realloc((void*)buffer,sizeof(T)*size); }
// Free memory \a buffer
template<class T> void unallocate(T* buffer) { free((void*)buffer); }
#endif

// Gets a pointer to \a value even if T override operator& (e.g array overrides & to return a pointer to its data buffer)
template<class T> T* addressof(T& value) { return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char &>(value))); }

// clear

//raw buffer zero initialization //TODO: SSE
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
//unsafe  (ignoring constructors) raw value zero initialization
template <class T> void clear(T& dst) { clear((byte*)addressof(dst),sizeof(T)); }
//safe buffer default initialization
template <class T> void clear(T* data, int count, const T& value=T()) { for(int i=0;i<count;i++) data[i]=value; }

// copy
//raw buffer copy //TODO: SSE
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
//unsafe (ignoring constructors) raw value copy
template <class T> void copy(T& dst,const T& src) { copy(addressof(dst),addressof(src),sizeof(T)); }

// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template <class T> T copy(const T& t) { return t; }
// explicit buffer copy
template <class T> void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }
