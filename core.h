#pragma once

/// Missing C++11 IDE support workarounds
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define for()
#define constexpr
#define override
#define _
#define DEBUG
#define i( ignore )
#define unused
#else
#define i( ignore... ) ignore
#define unused __attribute((unused))
#endif

/// Language support
#define declare(function, attributes...) function __attribute((attributes)); function
#define offsetof(object, member) __builtin_offsetof (object, member)
// Traits
#include <type_traits>
#define is_same(A,B) std::is_same<A,B>::value
#define is_convertible(F,T) std::is_convertible<F,T>::value
#define remove_reference(T) typename std::remove_reference<T>::type
// Predicates
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler
#define can_forward(T) is_convertible(remove_reference(T), remove_reference(T##f))
#define perfect(T) class T##f, predicate(can_forward(T))
#define perfect2(T,U) class T##f, class U##f, predicate(can_forward(T)), predicate1(can_forward(U))
// Move semantics
#include <bits/move.h>
using std::move;
using std::forward;
/*template<class T> inline constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)t; }
template<class T> inline constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<class T> inline constexpr T&& forward(remove_reference(T)&& t){static_assert(!std::is_lvalue_reference<T>::value,""); return (T&&)t; }*/
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;
#define default_constructors(o) o(){} o(o&&)=default;

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
typedef long long int64;
typedef unsigned long long uint64;
#if __WORDSIZE == 64
typedef unsigned long size_t; typedef long ssize_t; typedef unsigned long ptr;
#else
typedef unsigned int size_t; typedef int ssize_t; typedef unsigned int ptr;
#endif

#define swap32 __builtin_bswap32
inline uint16 swap16(uint16 x) { return swap32(x)>>16; }

#define floor __builtin_floorf
#define round __builtin_roundf
#define ceil __builtin_ceilf

/// Basic operations
template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template <class T> T abs(T x) { return x>=0 ? x : -x; }

/// SIMD
typedef float float4 __attribute__ ((vector_size(16)));
typedef double double2 __attribute__ ((vector_size(16)));
#define xor_ps __builtin_ia32_xorps
#define xor_pd __builtin_ia32_xorpd
#define loadu_ps __builtin_ia32_loadups
#define loadu_pd __builtin_ia32_loadupd
#define loada_ps(e) (*(float4*)(e))
#define movehl_ps __builtin_ia32_movhlps
#define shuffle_ps __builtin_ia32_shufps
#define extract_s __builtin_ia32_vec_ext_v4sf
#define extract_d __builtin_ia32_vec_ext_v2df

/// Debug
#ifdef DEBUG
/// compile \a statements in executable only if \a DEBUG flag is set
#define debug( statements... ) statements
/// compile \a statements in executable only if \a DEBUG flag is not set
#else
#define debug( statements... )
#endif

#ifdef TRACE
extern bool trace_enable;
#define trace_on trace_enable=true
#define trace_off trace_enable=false
#else
#define trace_on
#define trace_off
#endif

void logTrace(int skip=1);
/// Aborts the process without any message, stack trace is logged
#define fail() ({debug( trace_off; logTrace(); )  __builtin_abort(); })

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

/// Clear
//raw buffer zero initialization //TODO: SSE
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
//unsafe  (ignoring constructors) raw value zero initialization
template <class T> void clear(T& dst) { clear((byte*)&dst,sizeof(T)); }
//safe buffer default initialization
template <class T> void clear(T* data, int count, const T& value=T()) { for(int i=0;i<count;i++) data[i]=value; }

/// Copy
//raw buffer copy //TODO: SSE
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
//unsafe (ignoring constructors) raw value copy
template <class T> void copy(T& dst,const T& src) { copy((byte*)&dst,(byte*)&src,sizeof(T)); }
// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template <class T> T copy(const T& t) { return t; }
// explicit buffer copy
template <class T> void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }

/// Compare
//raw buffer comparison //TODO: SSE
inline bool compare(const byte* a,const byte* b, int size) { for(int i=0;i<size;i++) if(a[i]!=b[i]) return false; return true; }
//raw value comparison
template <class T> bool compare(const T& a,const T& b) { return compare((const byte*)&a,(const byte*)&b,sizeof(T)); }
