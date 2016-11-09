#pragma once
/// \file memory.h Memory operations and management (mref, buffer, unique, shared)
#include "core.h"

// C runtime memory allocation
extern "C" void* malloc(size_t size) noexcept;
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size) noexcept;
extern "C" void free(void* buffer) noexcept;

#if __INTEL_COMPILER
#define __atomic_fetch_add __atomic_fetch_add_explicit
#endif

/// Managed fixed capacity mutable reference to an array of elements
/// \note Data is either an heap allocation managed by this object or a reference to memory managed by another object.
generic struct buffer : mref<T> {
 using mref<T>::data;
 using mref<T>::size;
 size_t capacity = 0; /// 0: reference, >0: size of the owned heap allocation

 using mref<T>::at;
 using mref<T>::set;
 using mref<T>::slice;

 no_copy(buffer);
 constexpr buffer(){}
 buffer(buffer&& o) : mref<T>(o), capacity(o.capacity) { o.data=0; o.size=0; o.capacity=0; }
 constexpr buffer(T* data, size_t size, size_t capacity) : mref<T>(data, size), capacity(capacity) {}

 /// Allocates an uninitialized buffer for \a capacity elements
 buffer(size_t capacity, size_t size) : mref<T>((T*)0, size), capacity(capacity) {
  assert(capacity>=size);
  if(capacity && posix_memalign((void**)&data, 64, capacity*sizeof(T)))
   error("Out of memory", size, capacity, sizeof(T));
  assert_(size_t(data)%32==0);
 }
 /*explicit*/ buffer(size_t size) : buffer(size, size) {}

 buffer& operator=(buffer&& o) { this->~buffer(); new (this) buffer(::move(o)); return *this; }

 /// If the buffer owns the reference, returns the memory to the allocator
 ~buffer() {
  if(capacity) {
   if(!__has_trivial_destructor(T)) for(size_t i: range(size)) at(i).~T();
   free((void*)data);
  }
  data=0; capacity=0; size=0;
 }

 /// Appends a default element
 T& append() { return set(__atomic_fetch_add(&size, 1, 5/*SeqCst*/), T()); }

 /// Appends an implicitly copiable value
 T& append(const T& e) { return set(__atomic_fetch_add(&size, 1, 5/*SeqCst*/), e); }

 /// Appends a movable value
 T& append(T&& e) { return set(__atomic_fetch_add(&size, 1, 5/*SeqCst*/), ::move(e)); }

 /// Appends another list of elements to this array by moving
 void append(const mref<T> source) {
  slice(__atomic_fetch_add(&size, source.size, 5/*SeqCst*/), source.size).move(source);
 }

 /// Appends another list of elements to this array by copying
 void append(const ref<T> source) {
  slice(__atomic_fetch_add(&size, source.size, 5/*SeqCst*/), source.size).copy(source);
 }
#define appendAtomic append // atomic by default
};

typedef buffer<char> String;

/// Initializes a new buffer with the content of \a o
generic buffer<T> copy(const buffer<T>& o) { buffer<T> t(o.capacity?:o.size, o.size); t.copy(o); return t; }

/// Converts a reference to a buffer (unsafe as no automatic memory management method keeps the original reference from being released)
generic buffer<T> unsafeRef(const ref<T> o) { return buffer<T>((T*)o.data, o.size, 0); }

/// Initializes a new buffer moving the content of \a o
generic buffer<T> moveRef(mref<T> o) { buffer<T> copy(o.size); copy.mref<T>::move(o); return copy; }

/// Initializes a new buffer copying the content of \a o
generic buffer<T> copyRef(ref<T> o) { buffer<T> copy(o.size); copy.mref<T>::copy(o); return copy; }

// -- Apply --

/// Returns an array of the application of a function to every index up to a size
template<Type Function> auto apply(size_t size, Function function) -> buffer<decltype(function(0))> {
 buffer<decltype(function(0))> target(size);
 target.apply(function);
 return ::move(target);
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply(ref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
 buffer<decltype(function(source[0]))> target(source.size);
 target.apply(function, source);
 return ::move(target);
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply(mref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
 buffer<decltype(function(source[0]))> target(source.size);
 target.apply(function, source);
 return ::move(target);
}

/// Replaces in \a array every occurence of \a before with \a after
template<Type T> buffer<T> replace(ref<T> source, const T& before, const T& after) {
 return apply(source, [=](const T& e){ return e==before ? after : e; });
}

// -- Filter  --

/// Creates a new buffer containing only elements where filter \a predicate does not match
template<Type T, Type Function> buffer<T> filter(const ref<T> source, Function predicate) {
 buffer<T> target(source.size, 0); for(const T& e: source) if(!predicate(e)) target.append(copy(e)); return target;
}

// -- Join --

generic buffer<T> join(ref<ref<T>> list, const ref<T> separator) {
 if(!list) return {};
 size_t size = 0;
 for(auto e: list) {
  assert_(size < 4096 && e.size < 4096, e.size, list.size);
  size += e.size;
 }
 buffer<T> target (size + (list.size-1)*separator.size, 0);
 for(size_t i: range(list.size)) { target.append( list[i] ); if(i<list.size-1) target.append( separator ); }
 return target;
}

// -- Reinterpret casts

/// Reinterpret casts a const reference to another type
template<Type T, Type O> ref<T> cast(const ref<O> o) {
 assert((o.size*sizeof(O))%sizeof(T) == 0);
 return ref<T>((const T*)o.data, o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret casts a mutable reference to another type
template<Type T, Type O> mref<T> mcast(const mref<O>& o) {
 assert((o.size*sizeof(O))%sizeof(T) == 0);
 return mref<T>((T*)o.data, o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret casts a buffer to another type
template<Type T, Type O> buffer<T> cast(buffer<O>&& o) {
 buffer<T> buffer;
 buffer.data = (const T*)o.data;
 assert((o.size*sizeof(O))%sizeof(T) == 0);
 buffer.size = o.size*sizeof(O)/sizeof(T);
 assert((o.capacity*sizeof(O))%sizeof(T) == 0);
 buffer.capacity = o.capacity*sizeof(O)/sizeof(T);
 o.data=0; o.size=0; o.capacity = 0;
 return buffer;
}

// -- unique

/// Unique reference to an heap allocated value
generic struct unique {
 unique(decltype(nullptr)):pointer(0){}
 template<Type D> unique(unique<D>&& o):pointer(o.pointer){o.pointer=0;}
 template<Type... Args> explicit unique(Args&&... args) : pointer(new T(forward<Args>(args)...)) {}
 unique& operator=(unique&& o){ this->~unique(); new (this) unique(move(o)); return *this; }
 ~unique() { if(pointer) { delete pointer; } pointer=0; }

 operator T&() { return *pointer; }
 operator const T&() const { return *pointer; }
 T* operator ->() { assert_(pointer); return pointer; }
 const T* operator ->() const { assert_(pointer); return pointer; }
 explicit operator bool() const { return pointer; }
 bool operator !() const { return !pointer; }
 bool operator ==(const unique<T>& o) const { return pointer==o.pointer; }
 bool operator ==(const T* o) const { return pointer==o; }

 T* pointer;
};
generic unique<T> copy(const unique<T>& o) { return unique<T>(copy(*o.pointer)); }

// -- shared / shareable

/// Reference to a shared heap allocated value managed using a reference counter
/// \note the shared type must implement a reference counter (e.g. by inheriting shareable)
/// \note Move semantics are still used whenever adequate (sharing is explicit)
generic struct shared {
 shared(decltype(nullptr)):pointer(0){}
 template<Type D> shared(shared<D>&& o):pointer(dynamic_cast<T*>(o.pointer)){o.pointer=0;}
 template<Type... Args> explicit shared(Args&&... args):pointer(new (malloc(sizeof(T))) T(forward<Args>(args)...)){}
 shared& operator=(shared&& o){ this->~shared(); new (this) shared(move(o)); return *this; }
 explicit shared(const shared<T>& o) : pointer(o.pointer) { if(pointer) pointer->addUser(); }
 explicit shared(T* o) : pointer(o) { pointer->addUser();/*Unsafe as original owner might free*/ pointer->addUser(); }
 ~shared() { if(pointer) { assert(pointer->userCount); if(pointer->removeUser()==0) { pointer->~T(); free(pointer); } pointer=0; } }

 operator T&() { return *pointer; }
 operator const T&() const { return *pointer; }
 T* operator ->() { return pointer; }
 const T* operator ->() const { return pointer; }
 explicit operator bool() const { return pointer; }
 bool operator !() const { return !pointer; }
 bool operator ==(const shared<T>& o) const { return pointer==o.pointer; }

 T* pointer;
};
generic shared<T> copy(const shared<T>& o) { return shared<T>(copy(*o.pointer)); }
generic shared<T> share(const shared<T>& o) { return shared<T>(o); }

/// Reference counter to be inherited by shared objects
struct shareable {
 virtual void addUser() { ++userCount; }
 virtual uint removeUser() { return --userCount; }
 uint userCount = 1;
};

/// Aligns \a offset to \a width (only for power of two \a width)
inline size_t align(size_t width, size_t offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }
