#pragma once
/// \file core.h keywords, basic types, debugging, ranges

// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define notrace __attribute((no_instrument_function))
/// Less verbose template declarations
#define Type typename
/// Declares the default move constructor and move assigmment operator
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Move semantics
template<Type T> struct remove_reference { typedef T type; };
template<Type T> struct remove_reference<T&> { typedef T type; };
template<Type T> struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
template<Type T> inline constexpr Type remove_reference<T>::type&& move(T&& t) { return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
template<Type T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Base template for explicit copy (overriden by explicitly copyable types)
template<Type T> T copy(const T& o) { return o; }

// Forward
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
template<Type T> struct is_lvalue_reference<T&> { static constexpr bool value = true; };
/// Forwards references and copyable values
template<Type T> constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
template<Type T> constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }

// Comparison functions
template<Type A, Type B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }
template<Type T> T min(T a, T b) { return a<b ? a : b; }
template<Type T> T max(T a, T b) { return a>b ? a : b; }
template<Type T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<Type T> T abs(T x) { return x>=0 ? x : -x; }

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
namespace std { template<Type T> struct initializer_list { const T* data; size_t size; }; }
template<Type T> struct ref;
inline constexpr ref<byte> operator "" _(const char* data, size_t size);
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define _ // QtCreator doesn't parse custom literal operators (""_)
#endif

// Debugging
/// Logs a message to standard output
template<Type... Args> void log(const Args&... args);
template<> void log(const ref<byte>& message);
/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void error(const Args&... args)  __attribute((noreturn));
template<> void error(const ref<byte>& message) __attribute((noreturn));

#ifdef DEBUG
#define warn error
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({ if(!(expr)) error(#expr ""_, ##message); })
#else
#define warn log
#define assert(expr, message...) ({})
#endif

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
    iterator begin(){ return {start}; }
    iterator end(){ return {stop}; }
    uint start, stop;
};

/// Mutable memory range
template<Type T> struct memory {
    memory(T* start, T* stop) : start(start), stop(stop) {}
    T* begin() const { return start; }
    T* end() const { return stop; }
    T* start; T* stop;
};

/// Const memory range
template<Type T> struct ref {
    /// Default constructs an empty reference
    constexpr ref() : data(0), size(0) {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* data, uint size) : data(data), size(size) {}
    /// Converts an std::initializer_list to ref
    explicit constexpr ref(const std::initializer_list<T>& list) : data(list.data), size(list.size) {}

    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert(i<size); return data[i]; }
    explicit operator bool() const { return size; }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return ref<T>(data+pos,size); }
    /// Slices a reference to elements from to the end of the reference
    ref<T> slice(uint pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

    /// Compares all elements
    bool operator ==(const ref<T>& o) const {
        if(size != o.size) return false;
        for(uint i: range(size)) if(data[i]!=o.data[i]) return false;
        return true;
    }
    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& key) const { for(uint i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)>=0; }

    const T* data;
    uint size;
};

/// Returns const reference to a static string literal
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>(data,size); }
/// Returns const reference to memory used by \a t
template<Type T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

// Integer operations
/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

// Floating-point operations
inline float floor(float x) { return __builtin_floorf(x); }
inline float round(float x) { return __builtin_roundf(x); }
inline float ceil(float x) { return __builtin_ceilf(x); }
inline double floor(double x) { return __builtin_floor(x); }
inline double round(double x) { return __builtin_round(x); }
inline double ceil(double x) { return __builtin_ceil(x); }
inline double sqrt(double f) { return __builtin_sqrt(f); }
inline double pow(double x, double y) { return __builtin_pow(x,y); }
inline double exp(double x) { return __builtin_exp(x); }
inline double ln(double x) { return __builtin_log(x); }
inline double exp2(double x) { return __builtin_exp2(x); }
inline double log2(double x) { return __builtin_log2(x); }
#if __clang__
inline double exp10(double x) { return exp(x*ln(10)); }
#else
inline double exp10(double x) { return __builtin_exp10(x); }
#endif
inline double log10(double x) { return __builtin_log10(x); }
inline double cos(double t) { return __builtin_cos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
inline double mod(double q, double d) { return __builtin_fmod(q, d); }
const double PI = 3.14159265358979323846;

// Memory operations
// Initializes memory using a constructor (placement new)
inline void* operator new(size_t, void* p) { return p; }
/// Initializes raw memory to zero
inline void clear(byte* buffer, uint size) { for(byte& b: memory<byte>(buffer, buffer+size)) b=0; }
/// Initializes memory to \a value
template<Type T> void clear(T* buffer, uint size, const T& value=T()) { for(T& t: memory<T>(buffer, buffer+size)) new (&t) T(copy(value)); }
/// Copies values from \a src to \dst
template<Type T> void copy(T* dst,const T* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }

// C runtime memory allocation
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);

/// Reference type with move semantics
template<Type T> struct handle {
    handle(T pointer=T()):pointer(pointer){}
    handle& operator=(handle&& o){ pointer=o.pointer; o.pointer=0; return *this; }
    handle(handle&& o):pointer(o.pointer){o.pointer=T();}

    operator T() const { return pointer; }
    operator T&() { return pointer; }
    T* operator &() { return &pointer; }
    T operator ->() { return pointer; }

    T pointer;
};

/// Reference to a fixed capacity heap allocated buffer
template<Type T> struct buffer {
    buffer(){}
    buffer(T* data, uint capacity, uint size):data(data),capacity(capacity),size(size){}
    buffer(T* data, uint size):data(data),size(size){}
    buffer(buffer&& o):data(o.data),capacity(o.capacity),size(o.size){o.capacity=0;}
    buffer(uint capacity, uint size):capacity(capacity),size(size){ assert(capacity); if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error(""); }
    explicit buffer(uint size):buffer(size,size){}

    buffer(uint capacity, uint size, const T& value):buffer(capacity,size){clear(data,size,value);}

    buffer& operator=(buffer&& o){ this->~buffer(); new (this) buffer(move(o)); return *this; }
    ~buffer(){ if(capacity) free(data); data=0; }

    explicit operator bool() const { return data; }
    operator const T*() const { return data; }
    operator T*() { return data; }
    operator ref<T>() const { return ref<T>(data,size); }
    T* begin() const { return data; }
    T* end() const { return data+size; }
    T& operator[](uint i) { assert(i<size, i ,size); return (T&)data[i]; }

    /// Slices a mutable reference to elements from \a pos to \a pos + \a size
    memory<T> slice(uint pos, uint size) { assert(pos+size<=this->size); return memory<T>(data+pos, data+pos+size); }
    /// Slices a mutable reference to elements from \a pos the end of the array
    memory<T> slice(uint pos) { assert(pos<=size); return memory<T>(data+pos, data+size); }

    T* data=0;
    uint capacity=0;
    uint size=0;
};
template<Type T> buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity, o.size); copy(t.data,o.data,o.size); return t; }

/// Unique reference to an heap allocated value
template<Type T> struct unique {
    unique(unique&& o):pointer(o.pointer){o.pointer=0;}
    template<Type... Args> unique(Args&&... args):pointer(new T(forward<Args>(args)...)){}
    unique& operator=(unique&& o){ this->~unique(); new (this) unique(move(o)); return *this; }
    ~unique() { if(pointer) free(pointer); pointer=0; }

    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
    explicit operator bool() { return pointer; }
    bool operator !() const { return !pointer; }
    bool operator ==(const T* pointer) const { return this->pointer==pointer; }

    T* pointer;
};
template<Type T> unique<T> copy(const unique<T>& o) { return unique<T>(copy(*o.pointer)); }
