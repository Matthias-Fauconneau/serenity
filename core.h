#pragma once
/// \file core.h meta definitions, primitive types, log, range, std::initializer_list (aka ref) and other essential functions

// Attributes
#define unused __attribute((unused))
#define packed __attribute((packed))

// Integer types
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

// Keywords
#define Type typename
inline void* operator new(size_t, void* p) { return p; }

// Move semantics
template<Type T> struct remove_reference { typedef T type; };
template<Type T> struct remove_reference<T&> { typedef T type; };
template<Type T> struct remove_reference<T&&> { typedef T type; };
#define remove_reference(T) typename remove_reference<T>::type
template<Type T> inline __attribute((always_inline)) constexpr remove_reference(T)&& move(T&& t) { return (remove_reference(T)&&)(t); }
// Base template for explicit copy (overriden by explicitly copyable types)
template<Type T> T copy(const T& o) { return o; }
// Forward
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
template<Type T> struct is_lvalue_reference<T&> { static constexpr bool value = true; };
#define is_lvalue_reference(T) is_lvalue_reference<T>::value
template<Type T> constexpr T&& forward(remove_reference(T)& t) { return (T&&)t; }
template<Type T> constexpr T&& forward(remove_reference(T)&& t){ static_assert(!is_lvalue_reference(T),""); return (T&&)t; }
// Move constructors/operators declarators
#define no_copy(T) T(const T&)=delete; T& operator=(const T&)=delete
#define move_operator(T) T& operator=(T&& o){this->~T(); new (this) T(move(o)); return *this;}
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Initializer lists and string literals
namespace std { template<Type T> struct initializer_list; }
/// \a Const memory reference to contiguous elements
template<Type T> using ref = std::initializer_list<T>;
/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size);
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define _ // QtCreator doesn't parse operator _""
#endif

// Debugging
/// Logs to standard output
template<Type... Args> void log(const Args&... args);
template<> void log(const ref<byte>& message);
/// Logs to standard output and aborts immediatly this thread
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

/// Numeric range iterator
struct range {
    uint start, stop;
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
};

/// Memory range iterator
template<Type T> struct memory {
    T* start;
    T* stop;
    memory(T* start, T* stop) : start(start), stop(stop) {}
    T* begin() const { return start; }
    T* end() const { return stop; }
};

// Memory operations
/// Raw zero initialization
inline void clear(byte* buffer, uint size) { for(uint i: range(size)) buffer[i]=0; }
/// Buffer default initialization
template<Type T> void clear(T* buffer, uint size, const T& value=T()) { for(uint i: range(size)) new (buffer+i) T(copy(value)); }
/// Raw memory copy
inline void copy(byte* dst,const byte* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }
/// Buffer explicit copy
template<Type T> void copy(T* dst,const T* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }

// initializer_list
namespace std {
template<Type T> struct initializer_list {
    const T* data;
    size_t size;
    constexpr initializer_list() : data(0), size(0) {}
    /// References \a size elements from const \a data pointer
    constexpr initializer_list(const T* data, size_t size) : data(data), size(size) {}
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
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
    /// Copies elements to \a target and increments pointer
    void cat(T*& target) const { ::copy(target,data,size); target+=size; }
};
}

/// Returns reference to string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>(data,size); }

/// References raw memory representation of \a t
template<Type T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }
/// Casts raw memory to \a T
template<Type T> const T& raw(const ref<byte>& a) { assert(a.size==sizeof(T)); return *(T*)a.data; }
/// Casts between element types
template<Type T, Type O> ref<T> cast(const ref<O>& o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

// Basic operations
template<Type T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template<Type T> T min(T a, T b) { return a<b ? a : b; }
template<Type T> T max(T a, T b) { return a>b ? a : b; }
template<Type T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template<Type T> T abs(T x) { return x>=0 ? x : -x; }
template<Type T> T sign(T x) { return x>=0 ? 1 : -1; }
template<Type A, Type B> bool operator !=(const A& a, const B& b) { return !(a==b); }
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }

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
inline double exp10(double x) { return __builtin_exp10(x); }
inline double log10(double x) { return __builtin_log10(x); }

/// Reference type with move semantics
template<Type T> struct handle {
   handle(T pointer=0):pointer(pointer){}
   move_operator(handle) handle(handle&& o):pointer(o.pointer){o.pointer=0;}
   operator T() const { return pointer; }
   operator T&() { return pointer; }
   T* operator &() { return &pointer; }
   T operator ->() { return pointer; }
   T pointer=0;
};

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);
// Typed memory allocation (without initialization)
template<Type T> T* allocate(uint size) { assert(size); return (T*)malloc(size*sizeof(T)); }
template<Type T> T* allocate64(uint size) { void* buffer; if(posix_memalign(&buffer,64,size*sizeof(T))) error(""); return (T*)buffer; }
template<Type T> void reallocate(T*& buffer, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<Type T> void unallocate(T*& buffer) { assert(buffer); free((void*)buffer); buffer=0; }

/// Reference to a fixed capacity heap allocated buffer
template<Type T> struct buffer {
    T* data=0;
    uint capacity=0;
    uint size=0;

    buffer(){}
    move_operator(buffer) buffer(buffer&& o):data(o.data),capacity(o.capacity),size(o.size){o.capacity=0;}
    explicit buffer(const buffer& o):buffer(o.capacity){size=o.size; copy(data,o.data,size);}

    buffer(T* data, uint capacity, uint size):data(data),capacity(capacity),size(size){}
    buffer(T* data, uint size):data(data),size(size){}

    buffer(uint capacity, uint size):data(allocate64<T>(capacity)),capacity(capacity),size(size){}
    explicit buffer(uint size):data(allocate64<T>(size)),capacity(size),size(size){}

    buffer(uint capacity, uint size, const T& value):data(allocate64<T>(capacity)),capacity(capacity),size(size){clear(data,size,value);}
    buffer(uint size, const T& value):data(allocate64<T>(size)),capacity(size),size(size){clear(data,size,value);}

    ~buffer(){if(capacity){unallocate(data);}}

    explicit operator bool() const { return data; }
    operator const T*() const { return data; }
    operator T*() { return data; }
    operator ref<T>() const { return ref<T>(data,size); }
    T* begin() const { return data; }
    T* end() const { return data+size; }

    /// Slices a mutable reference to elements from \a pos to \a pos + \a size
    memory<T> slice(uint pos, uint size) { assert(pos+size<=this->size); return {data+pos, data+pos+size}; }
    /// Slices a mutable reference to elements from \a pos the end of the array
    memory<T> slice(uint pos) { assert(pos<=size); return {data+pos, data+size}; }
    T& operator[](uint i) { assert(i<size); return (T&)data[i]; }
};

/// Unique reference to an heap allocated value
template<Type T> struct unique {
    no_copy(unique);
    T* pointer;
    move_operator(unique) unique(unique&& o):pointer(o.pointer){o.pointer=0;}
    //unique():pointer(0){}
    /*template<Type O> unique(unique<O>&& o){pointer=o.pointer; o.pointer=0;}
    template<Type O> unique& operator=(unique<O>&& o){this->~unique(); pointer=o.pointer; o.pointer=0; return *this;}*/
    template<Type... Args> unique(Args&&... args):pointer(new T(forward<Args>(args)...)){}
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
};
template<Type T> unique<T> copy(const unique<T>& o) { return unique<T>(copy(*o.pointer)); }
