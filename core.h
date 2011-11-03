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
template<typename... Args> struct delegate {
	void* _this;
	void (*method)(void*, Args...);
	template <class C> delegate(C* _this, void (C::*method)(Args...)) : _this((void*)_this), method((void(*)(void*, Args...))method) {}
};

/// language support

#define declare(function, attributes...) function __attribute((attributes)); function
#define no_trace(function) function __attribute((no_instrument_function)); function
no_trace(inline void* operator new(uint64, void* p)) { return p; }
extern void* enabler;
template<bool> struct predicate {};
template<> struct predicate<true> { typedef void* type; };
#define predicate(E) typename predicate<E>::type& condition = enabler
#define predicate1(E) typename predicate<E>::type& condition1 = enabler

/// move semantics

#include <type_traits>
template<typename T> constexpr typename std::remove_reference<T>::type&& move(T&& t){ return (typename std::remove_reference<T>::type&&)t; }
template<typename T> constexpr T&& forward(typename std::remove_reference<T>::type& t) { return (T&&)t; }
template<typename T> constexpr T&& forward(typename std::remove_reference<T>::type&& t) {
	static_assert(!std::is_lvalue_reference<T>::value,"forwarding an lvalue"); return (T&&)t;
}
#define no_copy(o) o(o&)=delete; o& operator=(const o&)=delete;
#define can_forward_(F,T) std::is_convertible<typename std::remove_reference<F>::type, typename std::remove_reference<T>::type>::value
#define can_forward(T) can_forward_(T##f,T)
#define perfect_(F,T) template<class F, predicate(can_forward_(F,T)) >
#define perfect(T) perfect_(T##f,T)
#define perfect2_(F,T,G,U) template<class F, class G, predicate(can_forward_(F,T)), predicate1(can_forward_(G,U)) >
#define perfect2(T,U) perfect2_(T##f,T,U##f,U)

/// memory allocation

extern "C" {
void* malloc(size_t size) throw();
void* realloc(void* ptr, size_t size) throw();
void free(void *ptr) throw();
}

/// algorithms

template <class T> void clear(T* data, int count) { raw d=(raw)data; for(uint i=0;i<count*sizeof(T);i++) d[i]=0; }
template <class T> void clear(T& data) { clear(&data,1); }
template <class T> void copy(T* dst,const T* src, int count) { raw d=(raw)dst,s=(raw)src; for(uint i=0;i<count*sizeof(T);i++) d[i]=s[i]; }
template <class T> void copy(T& dst,const T& src) { copy(&dst,&src,1); }
template <class T> T copy(const T& t) { return t; }
template <class T> void swap(T& a, T& b) { T t = move(a); a=move(b); b=move(t); }

template <class T> T abs(T x) { return x>=0 ? x : -x; }
template <class T> T min(T a, T b) { return a<b ? a : b; }
template <class T> T max(T a, T b) { return a>b ? a : b; }
template <class T> T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }

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
