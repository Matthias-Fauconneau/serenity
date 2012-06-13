#pragma once

/// Missing C++11 IDE support workarounds
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define for()
#define override
#define _
#define DEBUG
#define i( ignore )
#define unused
#else
#define i( ignore... ) ignore
#define unused __attribute((unused))
#define packed __attribute((packed))
#endif

/// Language support
#define weak(function) function __attribute((weak)); function
#define static_this static void static_this() __attribute((constructor)); static void static_this
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

#define swap32 __builtin_bswap32
inline uint16 swap16(uint16 x) { return swap32(x)>>16; }

/// Basic operations
template<class T> inline void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template<class T> inline T min(T a, T b) { return a<b ? a : b; }
template<class T> inline T max(T a, T b) { return a>b ? a : b; }
template<class T> inline T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<class T> inline T abs(T x) { return x>=0 ? x : -x; }

/// Mathematic primitives

inline int floor(float f) { return __builtin_floorf(f); }
inline int round(float f) { return __builtin_roundf(f); }
inline int ceil(float f) { return __builtin_ceilf(f); }

const double PI = 3.14159265358979323846;
inline float sin(float t) { return __builtin_sinf(t); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float atan(float f) { return __builtin_atanf(f); }

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

/// Clear
//raw buffer zero initialization //TODO: SSE
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
//unsafe  (ignoring constructors) raw value zero initialization
template<class T> inline void clear(T& dst) { clear((byte*)&dst,sizeof(T)); }
//safe buffer default initialization
template<class T> inline void clear(T* data, int count, const T& value=T()) { for(int i=0;i<count;i++) data[i]=value; }

/// Copy
//raw buffer copy //TODO: SSE
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
//unsafe (ignoring constructors) raw value copy
template<class T> inline void copy(T& dst,const T& src) { copy((byte*)&dst,(byte*)&src,sizeof(T)); }
// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template<class T> inline T copy(const T& t) { return t; }
// explicit buffer copy
template<class T> inline void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }

/// Compare
//raw memory comparison //TODO: SSE
inline bool compare(const byte* a,const byte* b, int size) { for(int i=0;i<size;i++) if(a[i]!=b[i]) return false; return true; }
//raw value comparison
//template<class A, class B> inline bool operator !=(const A& a, const B& b) { return !(a==b); }
