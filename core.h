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

/// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define weak(function) function __attribute((weak)); function
#define static_this static void static_this() __attribute((constructor)); static void static_this
#define offsetof(object, member) __builtin_offsetof (object, member)

/// Move
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<class T> constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)(t); }
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;
// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template<class T> inline T copy(const T& t) { return t; }

/// Predicate
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

/// Primitives
typedef signed char int8;
typedef unsigned char byte;
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
typedef unsigned long size_t;
#else
typedef unsigned int size_t;
#endif

/// Basic operations
template<class T> inline void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template<class T> inline T min(T a, T b) { return a<b ? a : b; }
template<class T> inline T max(T a, T b) { return a>b ? a : b; }
template<class T> inline T abs(T x) { return x>=0 ? x : -x; }
template<class A, class B> inline bool operator !=(const A& a, const B& b) { return !(a==b); }
template<class A, class B> inline bool operator <(const A& a, const B& b) { return b>a; }
template<class T> inline void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }

/// compile \a statements in executable only if \a DEBUG flag is set
#ifdef DEBUG
#define debug( statements... ) statements
#else
#define debug( statements... )
#endif
void logTrace();
int exit(int code) __attribute((noreturn));
#define assert_(expr) ({ debug( if(!(expr)) logTrace(), exit(-1); ) })
