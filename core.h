#pragma once
/// \file core.h move/forward operators, predicate, integer types, IDE workarounds, variadic log, numeric range, std::initializer_list (aka ref), fat string and basic operations
// Attributes
#define unused __attribute((unused))
#define _packed __attribute((packed))

// Move
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<class T> __attribute((always_inline)) constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)(t); }
template<class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
#define no_copy(T) T(const T&)=delete; T& operator=(const T&)=delete
#define move_operator_(T)                        T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&& o)
#define move_operator(T)       no_copy(T); T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&& o)
#define default_move(T) T(){} no_copy(T); T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;} T(T&&)____(=default)
/// base template for explicit copy (overriden by explicitly copyable types)
template<class T> T copy(const T& o) { return o; }

// Forward
namespace std {
template<typename T, T v> struct integral_constant { static constexpr T value = v; typedef integral_constant<T, v> type; };
template<typename T, T v> constexpr T integral_constant<T, v>::value;
typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;
template<typename> struct is_lvalue_reference : public false_type {};
template<typename T> struct is_lvalue_reference<T&> : public true_type {};
}
#define is_lvalue_reference(T) std::is_lvalue_reference<T>::value
template<class T> constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<class T> constexpr T&& forward(remove_reference(T)&& t){ static_assert(!is_lvalue_reference(T),""); return (T&&)t; }

// Predicate
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

// Integer types
typedef char byte;
typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef unsigned short ushort;
typedef signed int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef unsigned long ptr;
typedef signed long long int64;
typedef unsigned long long uint64;
#if __x86_64
typedef unsigned long size_t;
#else
typedef unsigned int size_t;
#endif

// Works around missing support for some C++11 features in QtCreator code model
#ifndef __GXX_EXPERIMENTAL_CXX0X__
template<class T> struct ref; //templated typedef using
#define _ //string literal operator _""
#define __( args... ) //member initialization constructor {}
#define ___ //variadic template arguments unpack operator ...
#define ____( ignore... ) //=default, constructor{} initializer
#else
namespace std { template<class T> struct initializer_list; }
/// \a Unmanaged const memory reference
template<class T> using ref = std::initializer_list<T>;
/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size);
#define __( args... ) { args }
#define ___ ...
#define ____( ignore... ) ignore
#endif

#ifdef DEBUG
#define debug( statements... ) statements
#define warn error
#else
/// Compiles \a statements in executable only if \a DEBUG flag is set
#define debug( statements... )
#define warn log
#endif
// Forward declarations to allow string literal messages without header dependencies on string.h/array.h/memory.h
/// Logs to standard output
template<class ___ Args> void log(const Args& ___ args);
template<> void log(const ref<byte>& message);
/// Logs to standard output and aborts immediatly this thread
template<class ___ Args> void error(const Args& ___ args)  __attribute((noreturn));
template<> void error(const ref<byte>& message) __attribute((noreturn));
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({debug( if(!(expr)) error(#expr ""_, ##message);)})

/// Numeric range iterations
struct range {
    uint start,stop;
    range(uint start, uint stop):start(start),stop(stop){}
    range(uint size):range(0,size){}
    struct iterator { uint i; uint operator*() {return i;} uint operator++(){return i++;} bool operator !=(const iterator& o) const{return i!=o.i;}};
    iterator begin(){ return __(start); }
    iterator end(){ return __(stop); }
};

// initializer_list
namespace std {
template<class T> struct initializer_list {
    const T* data;
    uint size;
    constexpr initializer_list() : data(0), size(0) {}
    /// References \a size elements from read-only \a data pointer
    constexpr initializer_list(const T* data, uint size) : data(data), size(size) {}
    /// References elements sliced from \a begin to \a end
    constexpr initializer_list(const T* begin,const T* end) : data(begin), size(uint(end-begin)) {}
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert(i<size); return data[i]; }
    explicit operator bool() const { return size; }
    /// Compares all elements
    bool operator ==(const initializer_list<T>& o) const {
        if(size != o.size) return false;
        for(uint i: range(size)) if(!(data[i]==o.data[i])) return false;
        return true;
    }
    /// Slices a reference to elements from \a pos to \a pos + \a size
    initializer_list<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return initializer_list<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    initializer_list<T> slice(uint pos) const { assert(pos<=size); return initializer_list<T>(data+pos,size-pos); }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& key) const { for(uint i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)>=0; }
};
}

#ifdef __GXX_EXPERIMENTAL_CXX0X__
/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>((byte*)data,size); }
#endif

// Basic operations
template<class T> constexpr T min(T a, T b) { return a<b ? a : b; }
template<class T> constexpr T max(T a, T b) { return a>b ? a : b; }
template<class T> constexpr T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<class T> constexpr T abs(T x) { return x>=0 ? x : -x; }
template<class T> constexpr T sign(T x) { return x>=0 ? 1 : -1; }
template<class A, class B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<class A, class B> bool operator >(const A& a, const B& b) { return b<a; }

/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

// Floating-point operations
inline int floor(float f) { return __builtin_floorf(f); }
inline int round(float f) { return __builtin_roundf(f); }
inline int ceil(float f) { return __builtin_ceilf(f); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }

inline float pow(float x, float y) { return __builtin_pow(x,y); }

// Trigonometric operations
const double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float acos(float t) { return __builtin_acosf(t); }
inline float asin(float t) { return __builtin_asinf(t); }
