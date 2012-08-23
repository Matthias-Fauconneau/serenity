#pragma once

/// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define nodebug __attribute((always_inline, nodebug))
#define weak(function) function __attribute((weak)); function
#define offsetof(object, member) __builtin_offsetof (object, member)
inline void* operator new(unsigned long, void* p) { return p; } //placement new

/// Move
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<class T> constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)(t); }
#define no_copy(o) o(const o&)=delete; o& operator=(const o&)=delete;
/// base template for explicit copy (may be overriden for not implicitly copyable types using template specialization)
template<class T> T copy(const T& t) { return t; }

/// Forward
template<typename T, T v> struct integral_constant { static constexpr T value = v; typedef integral_constant<T, v> type; };
template<typename T, T v> constexpr T integral_constant<T, v>::value;
typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;
template<typename> struct is_lvalue_reference : public false_type { };
template<typename T> struct is_lvalue_reference<T&> : public true_type { };
#define is_lvalue_reference(T) is_lvalue_reference<T>::value

template<class T> constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<class T> constexpr T&& forward(remove_reference(T)&& t){ static_assert(!is_lvalue_reference(T),""); return (T&&)t; }

/// Predicate
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

/// Primitives
typedef signed char int8;
typedef char byte;
typedef unsigned char uint8;
typedef unsigned char ubyte;
typedef signed short int16;
typedef unsigned short uint16;
typedef unsigned short ushort;
typedef signed int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef unsigned long ptr;
typedef signed long long int64;
typedef unsigned long long uint64;

/// compile \a statements in executable only if \a DEBUG flag is set
#ifdef DEBUG
#define debug( statements... ) statements
#else
#define debug( statements... )
#endif
/// Logs current stack trace skipping /a skip last frames
void trace(int skip=0, uint size=-1);
/// Simplified debug methods (avoid header dependencies on debug.h/string.h/array.h/memory.h)
void log_(const char*);
void exit_(int) __attribute((noreturn));
/// Aborts unconditionally and display \a message
#define error_(message) ({ trace(0,-1); log_(message); exit_(-1); })
/// Aborts if \a expr evaluates to false and display \a expr
#define assert_(expr) ({debug( if(!(expr)) error_(#expr); )})

/// initializer_list
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
    const T& operator [](uint i) const { assert_(i<size); return data[i]; }
    explicit operator bool() const { return size; }
    /// Compares all elements
    bool operator ==(const initializer_list<T>& o) const {
        if(size != o.size) return false;
        for(uint i=0;i<size;i++) if(!(data[i]==o.data[i])) return false;
        return true;
    }
    /// Compares to single value
    bool operator ==(const T& value) const { return size==1 && *data == value; }
    /// Slices a reference to elements from \a pos to \a pos + \a size
    initializer_list<T> slice(uint pos, uint size) const { assert_(pos+size<=this->size); return initializer_list<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    initializer_list<T> slice(uint pos) const { assert_(pos<=size); return initializer_list<T>(data+pos,size-pos); }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& value) const { for(uint i=0;i<size;i++) { if(data[i]==value) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& value) const { return indexOf(value)>=0; }
};
}

/// Missing C++11 IDE support workarounds
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define override
template<class T> struct ref;
#define _
#define __( args... )
#define ___
#define ____( ignore... )
#else
/// \a ref is a const typed bounded memory reference (i.e fat pointer)
/// \note As \a data is not owned, ref should be used carefully (only as argument, never as field)
template<class T> using ref = std::initializer_list<T>;
/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, unsigned long size) { return ref<byte>((byte*)data,size); }
#define __( args... ) { args }
#define ___ ...
#define ____( ignore... ) ignore
#endif

/// Basic operations
template<class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template<class T> T min(T a, T b) { return a<b ? a : b; }
template<class T> T max(T a, T b) { return a>b ? a : b; }
template<class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<class T> T abs(T x) { return x>=0 ? x : -x; }
template<class A, class B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<class A, class B> bool operator <(const A& a, const B& b) { return b>a; }

/// Raw buffer zero initialization
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
/// Unsafe (ignoring constructors) raw value zero initialization
template<class T> void clear(T& dst) { static_assert(sizeof(T)>8,""); clear((byte*)&dst,sizeof(T)); }
/// Safe buffer default initialization
template<class T> void clear(T* data, int size, const T& value=T()) { for(int i=0;i<size;i++) data[i]=value; }

/// Raw buffer copy
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
/// Unsafe (ignoring constructors) raw value copy
template<class T> void copy(T& dst,const T& src) { copy((byte*)&dst,(byte*)&src,sizeof(T)); }
/// Safe buffer copy
template<class T> void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=copy(src[i]); }
