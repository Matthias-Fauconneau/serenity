#pragma once
/// \file core.h keywords, basic types, debugging, ranges, ref

// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
/// Less verbose template declarations
#define Type typename
#define generic template<Type T>
/// Nicer abstract declaration
#define abstract =0
/// Declares the default move constructor and move assigmment operator
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Move semantics
generic struct remove_reference { typedef T type; };
generic struct remove_reference<T&> { typedef T type; };
generic struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
generic inline constexpr Type remove_reference<T>::type&& move(T&& t) { return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
generic void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Base template for explicit copy (overriden by explicitly copyable types)
generic T copy(const T& o) { return o; }

/// Reference type with move semantics
generic struct handle {
    handle(T pointer=T()):pointer(pointer){}
    handle& operator=(handle&& o){ pointer=o.pointer; o.pointer=0; return *this; }
    handle(handle&& o):pointer(o.pointer){o.pointer=T();}

    operator T() const { return pointer; }
    operator T&() { return pointer; }
    T* operator &() { return &pointer; }
    T operator ->() { return pointer; }

    T pointer;
};

// Forward
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
generic struct is_lvalue_reference<T&> { static constexpr bool value = true; };
/// Forwards references and copyable values
generic constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
generic constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }

template<Type A, Type B> bool operator !=(const A& a, const B& b) { return !(a==b); }
// Arithmetic functions
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }
generic bool inRange(T min, T x, T max) { return !(x<min) && x<max; }
generic T min(T a, T b) { return a<b ? a : b; }
generic T max(T a, T b) { return a<b ? b : a; }
generic T clip(T min, T x, T max) { return x < min ? min : max < x ? max : x; }
generic T abs(T x) { return x>=0 ? x : -x; }
generic inline constexpr T sq(const T& x) { return x*x; }

// Basic types
typedef char byte;
typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
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
namespace std { generic struct initializer_list { const T* data; size_t size; }; }
generic struct ref;
/// Convenient typedef for ref<byte> holding UTF8 text strings
typedef ref<byte> string;
inline constexpr string operator "" _(const char* data, size_t size);
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define _ // QtCreator doesn't parse custom literal operators (""_)
#endif

// Debugging
/// Logs a message to standard output without newline
void log_(const string& message);
/// Logs a message to standard output
template<Type... Args> void log(const Args&... args);
template<> void log(const string& message);
/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void error(const Args&... args)  __attribute((noreturn));
template<> void error(const string& message) __attribute((noreturn));

#if ASSERT
#define warn error
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })
#else
#define warn log
#define assert(expr, message...) ({})
#endif
/// Aborts if \a expr evaluates to false and logs \a expr and \a message (even in release)
#define assert_(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })

/// Numeric range
struct range {
    range(uint start, uint stop) : start(start), stop(stop){}
    range(uint size) : range(0, size){}
    struct iterator {
        uint i;
        uint operator*() { return i; }
        iterator& operator++() { i++; return *this; }
        bool operator !=(const iterator& o) const{ return i<o.i; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    uint start, stop;
};

/// Unmanaged fixed-size const reference to an array of elements
generic struct ref {
    /// Default constructs an empty reference
    constexpr ref() {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* data, size_t size) : data(data), size(size) {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* begin, const T* end) : data(begin), size(end-begin) {}
    /// Converts an std::initializer_list to ref
    constexpr ref(const std::initializer_list<T>& list) : data(list.data), size(list.size) {}
    /// Converts a static array to ref
    template<size_t N> ref(const T (&a)[N]):  ref(a,N) {}

    explicit operator bool() const { if(size) assert(data); return size; }
    operator const T*() const { return data; }

    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& at(size_t i) const { assert(i<size); return data[i]; }
    const T& operator [](size_t i) const { return at(i); }
    const T& first() const { return at(0); }
    const T& last() const { return at(size-1); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return ref<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    ref<T> slice(size_t pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

    /// Compares all elements
    bool operator ==(const ref<T>& o) const {
        if(size != o.size) return false;
        for(size_t i: range(size)) if(data[i]!=o.data[i]) return false;
        return true;
    }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& key) const { for(size_t i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)>=0; }

    const T* data = 0;
    size_t size = 0;
};

/// Returns const reference to a static string literal
inline constexpr string operator "" _(const char* data, size_t size) { return string(data,size); }
/// Returns const reference to memory used by \a t
generic ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// Declares a file to be embedded in the binary
#define FILE(name) static ref<byte> name() { \
    extern char _binary_ ## name ##_start[], _binary_ ## name ##_end[]; \
    return ref<byte>(_binary_ ## name ##_start,_binary_ ## name ##_end); \
}

// ref<Arithmetic> operations
generic const T& min(const ref<T>& a) { const T* min=&a.first(); for(const T& e: a) if(e < *min) min=&e; return *min; }
generic const T& max(const ref<T>& a) { const T* max=&a.first(); for(const T& e: a) if(*max < e) max=&e; return *max; }

// Integer operations
/// Aligns \a offset down to previous \a width wide step (only for power of two \a width)
inline uint floor(uint width, uint offset) { assert((width&(width-1))==0); return offset & ~(width-1); }
/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }
/// Returns floor(log2(v))
inline uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }
/// Computes the next highest power of 2
inline uint nextPowerOfTwo(uint v) { v--; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v++; return v; }
