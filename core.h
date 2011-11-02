#pragma once

/// workaround missing IDE support
#ifndef __GXX_EXPERIMENTAL_CXX0X__
#define for()
#define constexpr
#define override
#endif

/// primitive types

typedef signed char int8;
typedef unsigned char uint8;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef unsigned int uint;
typedef long int64;
typedef unsigned long uint64;
typedef unsigned long size_t;
typedef long ssize_t;
typedef uint8* raw;
template<class T> struct array;
typedef array<char> string;
#define _(s) string(s,sizeof(s)-1)

/// language support

// attributes
#define declare(function, attributes...) function __attribute((attributes)); function
#define no_trace(function) function __attribute((no_instrument_function)); function

// placement new
no_trace(inline void* operator new(uint64, void* p)) { return p; }

// predicates
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

// move semantics
template<typename T> struct remove_reference { typedef T type; };
template<typename T> struct remove_reference<T&> { typedef T type; };
template<typename T> struct remove_reference<T&&> { typedef T type; };
template<typename T> constexpr typename remove_reference<T>::type&& move(T&& t){ return (typename remove_reference<T>::type&&)t; }

template<typename> struct is_lvalue_reference { static const bool value = false; };
template<typename T> struct is_lvalue_reference<T&> { static const bool value = true; };
template<typename T> constexpr T&& forward(typename remove_reference<T>::type& t) { return (T&&)t; }
template<typename T> constexpr T&& forward(typename remove_reference<T>::type&& t) {
	static_assert(!is_lvalue_reference<T>::value,"forwarding an lvalue"); return (T&&)t;
}
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;

// perfect forwarding

#include <type_traits>
#define can_forward_(F,T) std::is_convertible<typename std::remove_reference<F>::type, typename std::remove_reference<T>::type>::value
#define can_forward(T) can_forward_(T##f,T)
#define perfect_(F,T) template<class F, predicate(can_forward_(F,T)) >
#define perfect(T) perfect_(T##f,T)
#define perfect2_(F,T,G,U) template<class F, class G, predicate(can_forward_(F,T)), predicate1(can_forward_(G,U)) >
#define perfect2(T,U) perfect2_(T##f,T,U##f,U)

// signals and slots
//#include <functional>
/*using namespace std::placeholders;
#define signal(Args...) array<std::function<void(Args)>>
#define connect(signal, slot, args...) signal << std::bind(&std::remove_reference<decltype(*this)>::type::slot, this , ## args);
#define emit(signal, args...) ({ for(auto slot: signal) slot(args); })*/

/*template<typename... Args> struct slot {
	virtual operator(Args... args) =0;
}
template<typename... Args> struct signal : array<slot<Args...> > {
	void emit(Args... args) { for(auto slot: *this) slot(args...);  }
}
template<typename Class, typename... Args> struct slot {}
#define signal(Args...) signal<Args>
#define connect(signal, slot, args...) signal << std::bind(&std::remove_reference<decltype(*this)>::type::slot, this , ## args);
#define emit(signal, args...) signal.emit(args);*/

template<typename... Args> struct delegate {
	void* _this;
	void (*method)(void*, Args...);
	template <class C> delegate(C* _this, void (C::*method)(Args...)) : _this((void*)_this), method((void(*)(void*, Args...))method) {}
};
template<typename... Args> struct signal : array< delegate<Args...> > {
	void emit(Args... args) { for(auto slot: *this) slot.method(slot._this, args...);  }
	template <class C> void connect(C* _this, void (C::*method)(Args...)) {
		*this << delegate<Args...>(_this, method);
	}
};

//template<typename Class, typename... Args> struct slot {}
#define signal(Args...) signal<Args>
#define connect(signal, slot) signal.connect(this, &std::remove_reference<decltype(*this)>::type::slot);
//#define connect(signal, slot) signal << delegate(this, &std::remove_reference<decltype(*this)>::type::slot);
#define emit(signal, args...) signal.emit(args);

/// memory

extern "C" {
void* malloc(size_t size) throw();
void* realloc(void* ptr, size_t size) throw();
void free(void *ptr) throw();
void *memcpy(void*__restrict dst, const void*__restrict src, size_t n) throw();
void *memmove(void *dest, const void *src, size_t n) throw();
void *memset (void* s, int c, size_t n) throw();
}

/// algorithms

template <class T> T abs(T x) { return x>=0 ? x : -x; }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
template <class T> T sqr(T x) { return x*x; }
template <class T> void copy(T* dst,const T* src, int count) { memcpy(dst,src,(size_t)count*sizeof(T)); }
template <class T> void clear(T& dst) { memset(&dst,0,sizeof(T)); }
template <class T> void clear(T* dst, int count) { memset(dst,0,(size_t)count*sizeof(T)); }
template <class T> T copy(const T& t) { return t; }
template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }
template <class T> void reverse(T& a) { for(int i=0; i<a.size/2; i++) swap(a[i], a[a.size-i-1]); }

template <class T, predicate(sizeof(T)==4)> constexpr uint32 swap(T x) {
	return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24);
}
template <class T, predicate(sizeof(T)==2)> constexpr uint16 swap(T x) { return ((x>>8)&0xff)|((x&0xff)<<8); }
template <class T, predicate(sizeof(T)==1)> constexpr uint8 swap(T t) { return t; }

/// debugging support

void log_(bool);
void log_(char);
void log_(int8);
void log_(uint8);
void log_(int16);
void log_(uint16);
void log_(int32);
void log_(uint32);
void log_(int64);
void log_(uint64);
void log_(void* s);
void log_(float);
void log_(double);
void log_(const char* s);

template<class A, class... Args> void log_(const A& a, const Args&... args) { log_(a); log_(' '); log_(args...); }
template<class... Args> void log(const Args&... args) { log_(args...); log_('\n'); }

#if TRACE
extern bool trace_enable;
#define trace_on  trace_enable=true;
#define trace_off trace_enable=false;
#else
#define trace_on
#define trace_off
#endif
void logTrace();

extern "C" void abort() throw() __attribute((noreturn));
#define fail(args...) ({ trace_off; logTrace(); log_("Critical Failure:\t"); log(args); abort(); })
#undef assert
#define assert(expr, args...) ({ if(!(expr)) { trace_off; logTrace(); log_("Assertion Failure:\t"); log(#expr, ## args); abort(); } })

/// virtual iteration

// abstract interface for iterators
template<class T> struct abstract_iterator {
	raw index;
	abstract_iterator(void* index) : index((raw)index) {}
	virtual ~abstract_iterator() {}
	virtual T& operator* () const =0;
	virtual const abstract_iterator& operator++ () =0;
};
// abstract_iterator implementation for an array of T
template<class T> struct value_iterator : abstract_iterator<T> {
	int value_size;
	template <class D> value_iterator(D* index) : abstract_iterator<T>(index), value_size(sizeof(D)) {}
	T& operator* () const { return *(T*)this->index; }
	const abstract_iterator<T>& operator++() { this->index+=value_size; return *this; }
};
// abstract_iterator implementation dereferencing an array of T*
template<class T> struct dereference_iterator : abstract_iterator<T> {
	dereference_iterator(T** index) : abstract_iterator<T>(index) {}
	T& operator* () const { return **(T**)this->index; }
	const abstract_iterator<T>& operator++() { this->index+=sizeof(T*); return *this; }
};
// virtual_iterator forwards to an abstract iterator (dynamic dispatch)
template<class T> struct virtual_iterator {
	no_copy(virtual_iterator)
	abstract_iterator<T>* iterator;
	virtual_iterator(virtual_iterator<T>&& o) : iterator(o.iterator) { o.iterator=0; }
	virtual_iterator(abstract_iterator<T>* iterator) : iterator(iterator) {}
	~virtual_iterator() { if(iterator) delete iterator; }
	bool operator!=(const virtual_iterator& o) const { return iterator->index != o.iterator->index; }
	T& operator* () const { return iterator->operator *(); }
	const virtual_iterator& operator++ () { iterator->operator ++(); return *this; }
};
