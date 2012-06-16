#pragma once

/// Missing C++11 IDE support workarounds
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define override
#define _
#define ___
#define i( ignore... )
#else
#define ___ ...
#define i( ignore... ) ignore
#endif

/// Additional keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define weak(function) function __attribute((weak)); function
#define static_this static void static_this() __attribute((constructor)); static void static_this
#define offsetof(object, member) __builtin_offsetof (object, member)

/// Move semantics
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<class T> constexpr remove_reference(T)&& move(T&& t)
 { return (remove_reference(T)&&)(t); }

template<typename T, T v> struct integral_constant {
    static constexpr T value = v;
    typedef integral_constant<T, v> type;
};
template<typename T, T v> constexpr T integral_constant<T, v>::value;
typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;
template<typename> struct is_lvalue_reference : public false_type { };
template<typename T> struct is_lvalue_reference<T&> : public true_type { };
template<typename> struct is_rvalue_reference : public false_type { };
template<typename T> struct is_rvalue_reference<T&&> : public true_type { };

template<class T> constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<class T> constexpr T&& forward(remove_reference(T)&& t){
    static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;

/// Predicates
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

/// Primitives
typedef signed char int8;
typedef signed char byte;
typedef unsigned char uint8;
typedef unsigned char ubyte;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef signed long long int64;
typedef unsigned long long uint64;
#if __WORDSIZE == 64
typedef unsigned long size_t; typedef long ssize_t;
#else
typedef unsigned int size_t; typedef int ssize_t;
#endif

/// Basic operations
template<class T> inline void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template<class T> inline T min(T a, T b) { return a<b ? a : b; }
template<class T> inline T max(T a, T b) { return a>b ? a : b; }
template<class T> inline T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<class T> inline T abs(T x) { return x>=0 ? x : -x; }
template<class A, class B> inline bool operator !=(const A& a, const B& b) { return !(a==b); }
template<class A, class B> inline bool operator <(const A& a, const B& b) { return b>a; }

/// Debug
#ifdef DEBUG
/// compile \a statements in executable only if \a DEBUG flag is set
#define debug( statements... ) statements
void logTrace(int skip=1);
/// compile \a statements in executable only if \a DEBUG flag is not set
#else
#define debug( statements... )
inline void logTrace() {}
#endif

/// Memory
#define _NEW
inline void* operator new(size_t, void* p) { return p; } //placement new
extern "C" void* malloc(size_t size) throw();
extern "C" void* realloc(void* buffer, size_t size) throw();
extern "C" void free(void* buffer) throw();

#ifdef TRACE_MALLOC
void* allocate_(int size, const char* type=0);
void* reallocate_(void* buffer, int oldsize, int size);
void unallocate_(void* buffer);
#include <typeinfo>
template<class T> inline T* allocate(int size) { return (T*)allocate_(size*sizeof(T), typeid(T).name()); }
template<class T> inline T* reallocate(const T* buffer, int oldSize, int size) { return (T*)reallocate_((void*)buffer,sizeof(T)*oldSize,sizeof(T)*size); }
template<class T> inline void unallocate(T* buffer) { unallocate_((void*)buffer); }
#else
// Allocates an uninitialized memory buffer for \a size elements
template<class T> inline T* allocate(int size) { return (T*)malloc(size*sizeof(T)); }
// Reallocates memory \a buffer to new \a size
template<class T> inline T* reallocate(const T* buffer, int, int size) { return (T*)realloc((void*)buffer,sizeof(T)*size); }
// Free memory \a buffer
template<class T> inline void unallocate(T* buffer) { free((void*)buffer); }
#endif

//raw buffer zero initialization //TODO: SSE
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
//unsafe (ignoring constructors) raw value zero initialization
template<class T> inline void clear(T& dst) { static_assert(sizeof(T)>8,""); clear((byte*)&dst,sizeof(T)); }
//safe buffer default initialization
template<class T> inline void clear(T* data, int count, const T& value=T()) { for(int i=0;i<count;i++) data[i]=value; }

//raw buffer copy //TODO: SSE
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
//unsafe (ignoring constructors) raw value copy
template<class T> inline void copy(T& dst,const T& src) { copy((byte*)&dst,(byte*)&src,sizeof(T)); }
// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template<class T> inline T copy(const T& t) { return t; }
// explicit buffer copy
template<class T> inline void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }

//raw memory comparison //TODO: SSE
inline bool compare(const byte* a,const byte* b, int size) { for(int i=0;i<size;i++) if(a[i]!=b[i]) return false; return true; }
