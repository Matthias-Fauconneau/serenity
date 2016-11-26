#pragma once
/// \file core.h Keywords, traits, move semantics, range, ref, debug

// Keywords
#ifdef PROFILE
#define notrace __attribute((no_instrument_function))
#else
#define notrace
#endif
#define inline inline notrace
#define unused __attribute((unused))
#define packed __attribute((packed))
#define Type typename
#define generic template<Type T>
#define abstract =0
#define default_move(T) T(T&&)=default; T& operator=(T&&)=default
#define no_copy(T) T(const T&)=delete; T& operator=(const T&)=delete; T(T&& o) = delete;

// Traits
template<Type> struct is_lvalue_reference { static constexpr bool value = false; };
generic struct is_lvalue_reference<T&> { static constexpr bool value = true; };

// Move semantics
generic struct remove_reference { typedef T type; };
generic struct remove_reference<T&> { typedef T type; };
generic struct remove_reference<T&&> { typedef T type; };
/// Allows move assignment
generic __attribute((warn_unused_result)) inline constexpr Type remove_reference<T>::type&& move(T&& t)
{ return (Type remove_reference<T>::type&&)(t); }
/// Swap values (using move semantics as necessary)
generic inline void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
/// Forwards references and copyable values
generic constexpr T&& forward(Type remove_reference<T>::type& t) { return (T&&)t; }
/// Forwards moveable values
generic constexpr T&& forward(Type remove_reference<T>::type&& t){static_assert(!is_lvalue_reference<T>::value,""); return (T&&)t; }
/// Base template for explicit copy (overriden by explicitly copyable types)
generic __attribute((warn_unused_result)) T copy(const T& o) { return o; }

/// Reference type with move semantics
generic struct handle {
 T pointer;

 handle(T pointer=T()) : pointer(pointer){}
 handle& operator=(handle&& o) { pointer=o.pointer; o.pointer={}; return *this; }
 handle(handle&& o) : pointer(o.pointer){ o.pointer=T(); }

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
typedef __SIZE_TYPE__ size_t;
constexpr size_t invalid = -1; // Invalid index
#define null nullptr

// -- Number arithmetic
template<Type A, Type B> bool operator >(const A& a, const B& b) { return b<a; }
template<Type A, Type B> bool operator >=(const A& a, const B& b) { return b<=a; }
generic inline constexpr T min(T a, T b) { return a<b ? a : b; }
generic inline constexpr T max(T a, T b) { return a<b ? b : a; }
generic T clamp(T min, T x, T max) { return x < min ? min : max < x ? max : x; }

/// Numeric range
struct range {
 inline range(int start, int stop) : start(start), stop(stop){}
 inline range(int size) : range(0, size) {}
 struct iterator {
  int i;
  inline int operator*() { return i; }
  inline iterator& operator++() { i++; return *this; }
  inline bool operator !=(const iterator& o) const { return i<o.i; }
 };
 inline iterator begin() const { return {start}; }
 inline iterator end() const { return {stop}; }
 explicit operator bool() const { return start < stop; }
 int size() { return stop-start; }
 int start, stop;
};

/// Numeric range
struct reverse_range {
 inline reverse_range(int start, int stop) : start(start), stop(stop){}
 inline reverse_range(int size) : reverse_range(size-1, -1){}
 struct iterator {
  int i;
  inline int operator*() { return i; }
  inline iterator& operator++() { i--; return *this; }
  inline bool operator !=(const iterator& o) const { return i>o.i; }
 };
 inline iterator begin() const { return {start}; }
 inline iterator end() const { return {stop}; }
 explicit operator bool() const { return start > stop; }
 int size() { return start-stop; }
 int start, stop;
};

// -- initializer_list
#ifndef _INITIALIZER_LIST
namespace std {
generic struct initializer_list {
 const T* data;
 size_t length;
 constexpr initializer_list(const T* data, size_t size) : data(data), length(size) {}
 constexpr size_t size() const noexcept { return length; }
 constexpr const T* begin() const noexcept { return data; }
 constexpr const T* end() const { return (T*)data+length; }
};
}
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
 inline constexpr Ref() {}
 /// References \a size elements from const \a data pointer
 inline constexpr Ref(const T* data, size_t size) : data(data), size(size) {}
 /// Converts a real std::initializer_list to ref
 constexpr Ref(const std::initializer_list<T>& list) : data(list.begin()), size(list.size()) {}
 /// Explicitly references a static array
 template<size_t N> explicit constexpr Ref(const T (&a)[N]) : Ref(a,N) {}

 explicit operator bool() const { return size; }
 explicit operator const T*() const { return data; }

 const T* begin() const { return data; }
 const T* end() const { return data+size; }
 inline const T& at(size_t i) const;
 inline const T& operator [](size_t i) const { return at(i); }
 const T& last() const { return at(size-1); }

 /// Slices a reference to elements from \a pos to \a pos + \a size
 inline ref<T> slice(size_t pos, size_t size) const;
 inline ref<T> sliceRange(size_t begin, size_t end) const;
 /// Slices a reference to elements from \a pos to the end of the reference
 inline ref<T> slice(size_t pos) const;

 struct reverse_ref {
  const T* start; const T* stop;
  struct iterator {
   const T* pointer;
   const T& operator*() { return *pointer; }
   iterator& operator++() { pointer--; return *this; }
   typedef __INTPTR_TYPE__ intptr_t;
   bool operator !=(const iterator& o) const { return intptr_t(pointer)>=intptr_t(o.pointer); }
  };
  iterator begin() const { return {start}; }
  iterator end() const { return {stop}; }
 };
 reverse_ref reverse() { return {end()-1, begin()}; }

 /// Returns the index of the first occurence of \a value. Returns invalid if \a value could not be found.
 template<Type K> size_t indexOf(const K& key) const { for(size_t index: range(size)) { if(data[index]==key) return index; } return invalid; }
 /// Returns whether the array contains an occurrence of \a value
 template<Type K> bool contains(const K& key) const { return indexOf(key) != invalid; }
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
 inline constexpr ref(const char* data, size_t size) : Ref<char>(data, size) {}
 /// Implicitly references a string literal
 template<size_t N> constexpr ref(char const (&a)[N]) : ref(a, N-1 /*Does not include trailling zero byte*/) {}
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
void log_(string message);
void log(string message);

// -- Debug

/// Logs a message to standard output and signals all threads to log their stack trace and abort
template<Type... Args> void  __attribute((noreturn)) error(const Args&... args);
template<> void __attribute((noreturn,no_instrument_function)) error(const string& message);

/// Aborts if \a expr evaluates to false and logs \a expr and \a message (even in release)
#define assert_(expr, message...) ({ if(!(expr)) error(#expr ""_, ## message); })
#if DEBUG
/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) assert_(expr, ## message)
#else
#define assert(expr, message...) ({})
#endif

// -- ref
generic inline const T& Ref<T>::at(size_t i) const { assert(i<size, i, size); return data[i]; }
generic inline ref<T> Ref<T>::slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return ref<T>(data+pos, size); }
generic inline ref<T> Ref<T>::sliceRange(size_t begin, size_t end) const { assert(end<=this->size); return ref<T>(data+begin, end-begin); }
generic inline ref<T> Ref<T>::slice(size_t pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }

// -- FILE

/// Declares a file to be embedded in the binary
#define FILE(name) static ref<byte> name() { \
 extern char _binary_ ## name ##_start[], _binary_ ## name ##_end[]; \
 return ref<byte>(_binary_ ## name ##_start,_binary_ ## name ##_end - _binary_ ## name ##_start); \
 }

// -- mref

/// Initializes memory using a constructor (placement new)
#ifndef _NEW
inline void* operator new(size_t, void* p) noexcept { return p; }
#endif

/// Unmanaged fixed-size mutable reference to an array of elements
generic struct mref : ref<T> {
 using ref<T>::data;
 using ref<T>::size;

 /// Default constructs an empty reference
 constexpr mref(){}
 /// References \a size elements from \a data pointer
 inline mref(T* data, size_t size) : ref<T>(data,size) {}
 /// Converts an std::initializer_list to mref
 constexpr mref(std::initializer_list<T>&& list) : ref<T>(list.begin(), list.size()) {}
 /// Converts a static array to ref
 template<size_t N> mref(T (&a)[N]): mref(a,N) {}

 explicit operator bool() const { assert(!size || data, size); return size; }
 explicit operator T*() const { return (T*)data; }
 T* begin() const { return (T*)data; }
 T* end() const { return (T*)data+size; }
 inline T& at(size_t i) const { return (T&)ref<T>::at(i); }
 inline T& operator [](size_t i) const { return at(i); }
 T& first() const { return at(0); }
 T& last() const { return at(size-1); }

 /// Slices a reference to elements from \a pos to \a pos + \a size
 inline mref<T> slice(size_t pos, size_t size) const { assert(pos+size <= this->size, pos, size, this->size); return mref<T>((T*)data+pos, size); }
 /// Slices a reference to elements from to the end of the reference
 mref<T> slice(size_t pos) const { assert(pos<=size); return mref<T>((T*)data+pos,size-pos); }
 /// Slices a reference to elements from \a start to \a stop
 mref<T> sliceRange(size_t start, size_t stop) const { return slice(start, stop-start); }

 /// Initializes the element at index
 T& set(size_t index, const T& value) const { return *(new (&at(index)) T(value)); } /// Initializes the element at index
 T& set(size_t index, T&& value) const { return *(new (&at(index)) T(::move(value))); }
 /// Initializes the element at index
 template<Type... Args> T& set(size_t index, Args&&... args) const { return *(new (&at(index)) T{forward<Args>(args)...}); }
 /// Initializes reference using the same constructor for all elements
 template<Type... Args> void clear(Args&&... args) const { for(T& e: *this) new (&e) T(forward<Args>(args)...); }
 /// Initializes reference from \a source using move constructor
 void move(const mref<T>& source) { assert(size==source.size); for(size_t index: range(size)) set(index, ::move(source[index])); }
 /// Initializes reference from \a source using copy constructor
 void copy(const ref<T> source) const { assert(size==source.size); for(size_t index: range(size)) set(index, ::copy(source[index])); }

 /// Stores the application of a function to every index up to a size
 template<Type Function> void apply(Function function) const { for(size_t index: range(size)) set(index, function(index)); }
 /// Stores the application of a function to every elements of a ref
 template<Type Function, Type... V> void apply(Function function, ref<V>... sources) const {
  for(size_t index: range(size)) new (&at(index)) T(function(sources[index]...));
 }
 /// Stores the application of a function to every elements of a mref
 template<Type Function, Type... V> void apply(Function function, mref<V>... sources) const {
  for(size_t index: range(size)) new (&at(index)) T(function(sources[index]...));
 }

 /// Replaces in \a array every occurence of \a before with \a after
 template<Type K> mref& replace(const K& before, const T& after) { for(T& e : *this) if(e==before) e=::copy(after); return *this; }
};
/// Returns mutable reference to memory used by \a t
generic mref<byte> raw(T& t) { return mref<byte>((byte*)&t,sizeof(T)); }
