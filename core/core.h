#pragma once
/// \file core.h Keywords, traits, move semantics, range, ref, debug

// Keywords
#define unused __attribute((unused))
#define packed __attribute((packed))
#define Type typename
#define generic template<Type T>
#define abstract =0
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default

// Traits
struct true_type { static constexpr bool value = true; };
struct false_type{ static constexpr bool value = false; };
template<Type> struct is_lvalue_reference : false_type {};
generic struct is_lvalue_reference<T&> : true_type {};
template<Type A, Type B> struct is_same : false_type {};
generic struct is_same<T, T> : true_type {};
template<bool B, Type T = void> struct enable_if {};
generic struct enable_if<true, T> { typedef T type; };
generic struct declval_protector {
	static const bool stop = false;
	static T&& delegate();
};
generic inline T&& declval() noexcept {
	static_assert(declval_protector<T>::__stop, "declval() must not be used!");
	return declval_protector<T>::__delegate();
}
template<Type From, Type To> struct is_convertible {
	template<Type T> static void test(T);
	template<Type F, Type T, Type = decltype(test<T>(declval<F>()))> static true_type test(int);
	template<Type, Type> static false_type test(...);
	static constexpr bool value = decltype(test<From, To>(0))::value;
};

// Move semantics
generic struct remove_reference { typedef T type; };
generic struct remove_reference<T&> { typedef T type; };
generic struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
generic inline constexpr Type remove_reference<T>::type&& __attribute__((warn_unused_result)) move(T&& t)
{ return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
generic void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Forwards references and copyable values
generic constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
generic constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }
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
#endif

// -- ref

generic struct Ref;
// Allows ref<char> template specialization to be implemented by Ref
generic struct ref : Ref<T> { using Ref<T>::Ref; };

/// Unmanaged fixed-size const reference to an array of elements
generic struct Ref {
    typedef T type;
    const T* data = 0;
    size_t size = 0;

    /// Default constructs an empty reference
	constexpr Ref() {}
    /// References \a size elements from const \a data pointer
	constexpr Ref(const T* data, size_t size) : data(data), size(size) {}
    /// Converts a real std::initializer_list to ref
	constexpr Ref(const std::initializer_list<T>& list) : data(list.begin()), size(list.size()) {}
    /// Explicitly references a static array
	template<size_t N> explicit constexpr Ref(const T (&a)[N]) : Ref(a,N) {}

    explicit operator bool() const { return size; }
    explicit operator const T*() const { return data; }

    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    const T& at(size_t i) const;
    const T& operator [](size_t i) const { return at(i); }
    const T& last() const { return at(size-1); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(size_t pos, size_t size) const;
    /// Slices a reference to elements from \a pos to the end of the reference
	ref<T> slice(size_t pos) const;

	struct reverse_ref {
		const T* start; const T* stop;
		struct iterator {
			const T* pointer;
			const T& operator*() { return *pointer; }
			iterator& operator++() { pointer--; return *this; }
			bool operator !=(const iterator& o) const { return intptr_t(pointer)>=intptr_t(o.pointer); }
		};
		iterator begin() const { return {start}; }
		iterator end() const { return {stop}; }
	};
	reverse_ref reverse() { return {end()-1, begin()}; }

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

/// ref discarding trailing zero byte in ref(char[N])
// Needs to be a template specialization as a direct derived class specialization prevents implicit use of ref(char[N]) to bind ref<char>
template<> struct ref<char> : Ref<char> {
	using Ref::Ref;
	constexpr ref() {}
	/// Implicitly references a string literal
	template<size_t N> constexpr ref(char const(&a)[N]) : ref(a, N-1 /*Does not include trailling zero byte*/) {}
};

/// Returns const reference to memory used by \a t
generic ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// ref<char> holding a UTF8 text string
typedef ref<char> string;

/// Returns const reference to a static string literal
inline constexpr string operator "" _(const char* data, size_t size) { return string(data,size); }

// -- Log

/// Logs a message to standard output
template<Type... Args> void log(const Args&... args);
void log(string message);

// -- Debug

/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void error(const Args&... args)  __attribute((noreturn));
template<> void error(const string& message) __attribute((noreturn));

/// Aborts if \a expr evaluates to false and logs \a expr and \a message (even in release)
#define assert_(expr, message...) ({ if(!(expr)) error(#expr ""_, ## message); })
#if DEBUG
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) assert_(expr, ## message)
#else
#define assert(expr, message...) ({})
#endif

// -- ref
generic const T& Ref<T>::at(size_t i) const { assert(i<size, i, size); return data[i]; }
generic ref<T> Ref<T>::slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return ref<T>(data+pos, size); }
generic ref<T> Ref<T>::slice(size_t pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

// -- FILE

/// Declares a file to be embedded in the binary
#define FILE(name) static ref<byte> name() { \
    extern char _binary_ ## name ##_start[], _binary_ ## name ##_end[]; \
    return ref<byte>(_binary_ ## name ##_start,_binary_ ## name ##_end); \
}
