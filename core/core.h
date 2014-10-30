#pragma once
/// \file core.h keywords, basic types, debugging, ranges, ref

// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define notrace __attribute((no_instrument_function))
#define Type typename
#define generic template<Type T>
#define abstract =0
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Move semantics
generic struct remove_reference { typedef T type; };
generic struct remove_reference<T&> { typedef T type; };
generic struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
generic inline constexpr Type remove_reference<T>::type&& __attribute__((warn_unused_result)) move(T&& t)
{ return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
generic void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Base template for explicit copy (overriden by explicitly copyable types)
generic T __attribute__((warn_unused_result))  copy(const T& o) { return o; }

/// Reference type with move semantics
generic struct handle {
    T pointer;

    handle(T pointer=T()):pointer(pointer){}
    handle& operator=(handle&& o){ pointer=o.pointer; o.pointer=0; return *this; }
    handle(handle&& o):pointer(o.pointer){o.pointer=T();}

    operator T() const { return pointer; }
    operator T&() { return pointer; }
    T* operator &() { return &pointer; }
    T operator ->() { return pointer; }
    const T operator ->() const { return pointer; }
};

// Forward
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
generic struct is_lvalue_reference<T&> { static constexpr bool value = true; };
/// Forwards references and copyable values
generic constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
generic constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }

template<Type A, Type B> constexpr bool operator !=(const A& a, const B& b) { return !(a==b); }

// -- Integer types
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
static_assert(sizeof(uint64)==8,"");
typedef __INTPTR_TYPE__  intptr_t;
typedef __SIZE_TYPE__ 	size_t;
constexpr size_t invalid = -1; // Invalid index

// -- Number arithmetic
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }
generic T min(T a, T b) { return a<b ? a : b; }
generic T max(T a, T b) { return a<b ? b : a; }
generic T clip(T min, T x, T max) { return x < min ? min : max < x ? max : x; }
generic T abs(T x) { return x>=0 ? x : -x; }

/// Numeric range
struct range {
    range(int start, int stop) : start(start), stop(stop){}
    range(uint size) : range(0, size){}
    struct iterator {
        int i;
        int operator*() { return i; }
        iterator& operator++() { i++; return *this; }
        bool operator !=(const iterator& o) const{ return i<o.i; }
    };
    iterator begin() const { return {start}; }
    iterator end() const { return {stop}; }
    explicit operator bool() const { return start < stop; }
	int size() { return stop-start; }
    int start, stop;
};

// -- initializer_list

#ifndef _INITIALIZER_LIST
namespace std { generic struct initializer_list {
    const T* data;
    size_t length;
    constexpr initializer_list(const T* data, size_t size) : data(data), length(size) {}
    constexpr size_t size() const noexcept { return length; }
    constexpr const T* begin() const noexcept { return data; }
    constexpr const T* end() const { return (T*)data+length; }
}; }
generic struct ref;
#endif

// -- ref

/// Unmanaged fixed-size const reference to an array of elements
generic struct ref {
    typedef T type;
    const T* data = 0;
    size_t size = 0;

    /// Default constructs an empty reference
    constexpr ref() {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const T* data, size_t size) : data(data), size(size) {}
    /// References \a size elements from const \a data pointer
    //constexpr ref(const T* begin, const T* end) : data(begin), size(end-begin) {}
    /// Converts a real std::initializer_list to ref
    constexpr ref(const std::initializer_list<T>& list) : data(list.begin()), size(list.size()) {}
    /// Explicitly references a static array
    template<size_t N> explicit constexpr ref(const T (&a)[N]) : ref(a,N) {}

    explicit operator bool() const { return size; }
    explicit operator const T*() const { return data; }

    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& at(size_t i) const;
    //T value(size_t i, T defaultValue) const { return i<size ? data[i] : defaultValue; }
    const T& operator [](size_t i) const { return at(i); }
    const T& last() const { return at(size-1); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(size_t pos, size_t size) const;
    /// Slices a reference to elements from \a pos to the end of the reference
	ref<T> slice(size_t pos) const;

    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    size_t indexOf(const T& key) const { for(size_t i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& key) const { return indexOf(key)!=invalid; }
    /// Compares all elements
    bool operator ==(const ref<T> o) const {
        if(size != o.size) return false;
        for(size_t i: range(size)) if(data[i]!=o.data[i]) return false;
        return true;
    }
};

// -- string

template<> struct ref<char> {
    typedef char type;
    const char* data = 0;
    size_t size = 0;

    /// Default constructs an empty reference
    constexpr ref() {}
    /// References \a size elements from const \a data pointer
    constexpr ref(const char* data, size_t size) : data(data), size(size) {}
    /// Implicitly references a string literal
    template<size_t N> constexpr ref(char const(&a)[N]) : ref(a, N-1 /*Does not include trailling zero byte*/) {}

    explicit operator bool() const { return size; }

    const char* begin() const { return data; }
    const char* end() const { return data+size; }
    const char& at(size_t i) const;
    const char& operator [](size_t i) const { return at(i); }
    const char& last() const { return at(size-1); }
    ref<char> slice(size_t pos, size_t size) const;
    ref<char> slice(size_t pos) const;

    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    size_t indexOf(const char& key) const { for(size_t i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const char& key) const { return indexOf(key)!=invalid; }

    bool operator ==(const ref<char> o) const {
        if(size != o.size) return false;
        for(size_t i: range(size)) if(data[i]!=o.data[i]) return false;
        return true;
    }
};

/// Returns const reference to memory used by \a t
generic ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// ref<char> holding a UTF8 text string
typedef ref<char> string;
/// Returns const reference to a static string literal
inline constexpr string operator "" _(const char* data, size_t size) { return string(data,size); }

// -- Log

/// Logs a message to standard output without newline
void log_(string message);
/// Logs a message to standard output
template<Type... Args> void log(const Args&... args);
void log(string message);

// -- Debug

/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void error(const Args&... args)  __attribute((noreturn));
template<> void error(const string& message) __attribute((noreturn));

#if DEBUG
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({ if(!(expr)) error(#expr, ##message); })
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define warn(expr, message...) ({ if(!(expr)) error(#expr, ##message); })
#define debug(statements...) statements
#else
#define assert(expr, message...) ({})
/// Warns if \a expr evaluates to false and logs \a expr and \a message
#define warn(expr, message...) ({ if(!(expr)) log(#expr, ##message); })
#define debug(statements...)
#endif
/// Aborts if \a expr evaluates to false and logs \a expr and \a message (even in release)
#define assert_(expr, message...) ({ if(!(expr)) error(#expr ""_ , ##message); })

// -- ref
generic const T& ref<T>::at(size_t i) const { assert(i<size, i, size); return data[i]; }
generic ref<T> ref<T>::slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return ref<T>(data+pos, size); }
generic ref<T> ref<T>::slice(size_t pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

inline const char& ref<char>::at(size_t i) const { assert(i<size, i, size); return data[i]; }
inline ref<char> ref<char>::slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return ref<char>(data+pos, size); }
inline ref<char> ref<char>::slice(size_t pos) const { assert(pos<=size); return ref<char>(data+pos,size-pos); }

// -- FILE

/// Declares a file to be embedded in the binary
#define FILE(name) static ref<byte> name() { \
    extern char _binary_ ## name ##_start[], _binary_ ## name ##_end[]; \
    return ref<byte>(_binary_ ## name ##_start,_binary_ ## name ##_end); \
}

// -- mref

#ifndef _NEW
/// Initializes memory using a constructor (placement new)
inline void* operator new(size_t, void* p) noexcept { return p; }
#endif

/// Unmanaged fixed-size mutable reference to an array of elements
generic struct mref : ref<T> {
    using ref<T>::data;
    using ref<T>::size;

    /// Default constructs an empty reference
    mref(){}
    /// References \a size elements from \a data pointer
    mref(T* data, size_t size) : ref<T>(data,size) {}
    /// Converts an std::initializer_list to mref
    constexpr mref(std::initializer_list<T>&& list) : ref<T>(list.begin(), list.size()) {}
    /// Converts a static array to ref
    template<size_t N> mref(T (&a)[N]): mref(a,N) {}

    explicit operator bool() const { assert(!size || data, size); return size; }
    explicit operator T*() const { return (T*)data; }
    T* begin() const { return (T*)data; }
    T* end() const { return (T*)data+size; }
    T& at(size_t i) const { return (T&)ref<T>::at(i); }
    T& operator [](size_t i) const { return at(i); }
    T& first() const { return at(0); }
    T& last() const { return at(size-1); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    mref<T> slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return mref<T>((T*)data+pos, size); }
    /// Slices a reference to elements from to the end of the reference
    mref<T> slice(size_t pos) const { assert(pos<=size); return mref<T>((T*)data+pos,size-pos); }
    /// Slices a reference to elements from \a start to \a stop
	mref<T> sliceRange(size_t start, size_t stop) const { return slice(start, stop-start); }

	/// Initializes the element at index
	T& set(size_t index, const T& value) const { return *(new (&at(index)) T(value)); }
	/// Initializes the element at index
	T& set(size_t index, T&& value) const { return *(new (&at(index)) T(::move(value))); }
	/// Initializes the element at index
	template<Type... Args> T& set(size_t index, Args&&... args) const { return *(new (&at(index)) T{forward<Args>(args)...}); }
    /// Initializes reference using the same constructor for all elements
    template<Type... Args> void clear(Args... args) const { for(T& e: *this) new (&e) T(args...); }
    /// Initializes reference from \a source using move constructor
    void move(const mref<T>& source) { assert(size==source.size); for(size_t index: range(size)) set(index, ::move(source[index])); }
    /// Initializes reference from \a source using copy constructor
    void copy(const ref<T> source) const { assert(size==source.size); for(size_t index: range(size)) set(index, ::copy(source[index])); }

    /// Stores the application of a function to every index up to a size in a mref
    template<Type Function> void apply(Function function) const { for(size_t index: range(size)) set(index, function(index)); }
    /// Stores the application of a function to every elements of a ref in a mref
    template<Type Function, Type... S> void apply(Function function, ref<S>... sources) const {
        for(size_t index: range(size)) new (&at(index)) T(function(sources[index]...));
    }
	/// Stores the application of a function to every elements of a ref in a mref
	template<Type Function, Type... S> void apply(Function function, mref<S>... sources) const {
		for(size_t index: range(size)) new (&at(index)) T(function(sources[index]...));
	}

    /// Replaces in \a array every occurence of \a before with \a after
    template<Type B, Type A> mref& replace(const B& before, const A& after) { for(T& e : *this) if(e==before) e=T(after); return *this; }
};
/// Returns mutable reference to memory used by \a t
generic mref<byte> raw(T& t) { return mref<byte>((byte*)&t,sizeof(T)); }
