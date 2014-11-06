#pragma once
/// \file memory.h Memory operations and management (mref, buffer, unique, shared)
#include "core.h"

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
	template<Type K> mref& replace(const K& before, const T& after) { for(T& e : *this) if(e==before) e=::copy(after); return *this; }
};
/// Returns mutable reference to memory used by \a t
generic mref<byte> raw(T& t) { return mref<byte>((byte*)&t,sizeof(T)); }

// C runtime memory allocation
extern "C" void* malloc(size_t size) noexcept;
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size) noexcept;
extern "C" void* realloc(void* buffer, size_t size) noexcept;
extern "C" void free(void* buffer) noexcept;
#include <type_traits>
/// Managed fixed capacity mutable reference to an array of elements
/// \note Data is either an heap allocation managed by this object or a reference to memory managed by another object.
generic struct buffer : mref<T> {
    using mref<T>::data;
    using mref<T>::size;
    size_t capacity = 0; /// 0: reference, >0: size of the owned heap allocation

	using mref<T>::at;
	using mref<T>::set;
	using mref<T>::slice;

    buffer(){}
    buffer(buffer&& o) : mref<T>(o), capacity(o.capacity) { o.data=0, o.size=0, o.capacity=0; }
	buffer(T* data, size_t size, size_t capacity) : mref<T>(data, size), capacity(capacity) {}

    /// Allocates an uninitialized buffer for \a capacity elements
	buffer(size_t capacity, size_t size) : mref<T>((T*)0, size), capacity(capacity) {
		assert(capacity>=size && size>=0); if(!capacity) return;
		if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error("Out of memory", size, capacity, sizeof(T));
    }
	explicit buffer(size_t size) : buffer(size, size) {}

    buffer& operator=(buffer&& o) { this->~buffer(); new (this) buffer(::move(o)); return *this; }

    /// If the buffer owns the reference, returns the memory to the allocator
    ~buffer() {
        if(capacity) {
			if(!__has_trivial_destructor(T)) for(size_t i: range(size)) at(i).~T();
			free((void*)data);
        }
        data=0; capacity=0; size=0;
    }

	void setSize(size_t size) { assert_(size<=capacity, size, capacity); this->size=size; }
	/// Appends a default element
	T& append() { setSize(size+1); return set(size-1, T()); }
	/// Appends an implicitly copiable value
	T& append(const T& e) { setSize(size+1); return set(size-1, e); }
	/// Appends a movable value
	T& append(T&& e) { setSize(size+1); return set(size-1, ::move(e)); }
	/// Appends another list of elements to this array by moving
	void append(const mref<T> source) { setSize(size+source.size); slice(size-source.size).move(source); }
	/// Appends another list of elements to this array by copying
	void append(const ref<T> source) { setSize(size+source.size); slice(size-source.size).copy(source); }
	/*/// Appends a new element
	template<Type Arg, typename enable_if<!is_convertible<Arg, T>::value && !is_convertible<Arg, ref<T>>::value>::type* = nullptr>
	T& append(Arg&& arg) { setSize(size+1); return set(size-1, forward<Arg>(arg)); }
	/// Appends a new element
	template<Type Arg0, Type Arg1, Type... Args> T& append(Arg0&& arg0, Arg1&& arg1, Args&&... args) {
		setSize(size+1); return set(size-1, forward<Arg0>(arg0), forward<Arg1>(arg1), forward<Args>(args)...);
	}*/
};
/// Initializes a new buffer with the content of \a o
//generic buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity?:o.size, o.size); t.copy(o); return t; }
//generic buffer<T> copy(const buffer<T>& o){ return o.capacity ? copyRef(o) : unsafeRef(o); }
generic buffer<T> copy(const buffer<T>& o){ assert_(o.capacity); return copyRef(o); }

/// Converts a reference to a buffer (unsafe as no automatic memory management method keeps the original reference from being released)
generic buffer<T> unsafeRef(const ref<T> o) { return buffer<T>((T*)o.data, o.size, 0); }

/// Initializes a new buffer with the content of \a o
generic buffer<T> copyRef(ref<T> o) { buffer<T> copy(o.size); copy.mref<T>::copy(o); return copy; }

// -- Apply --

/// Returns an array of the application of a function to every index up to a size
template<Type Function> auto apply(size_t size, Function function) -> buffer<decltype(function(0))> {
	buffer<decltype(function(0))> target(size); target.apply(function); return target;
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply(ref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
	buffer<decltype(function(source[0]))> target(source.size); target.apply(function, source); return target;
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply(mref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
	buffer<decltype(function(source[0]))> target(source.size); target.apply(function, source); return target;
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
    o.data=0, o.size=0, o.capacity = 0;
    return buffer;
}

// -- String

typedef buffer<char> String;

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
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
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
    explicit shared(const shared<T>& o):pointer(o.pointer){ pointer->addUser(); }
    ~shared() { if(!pointer) return; assert(pointer->userCount); if(pointer->removeUser()==0) { pointer->~T(); free(pointer); } pointer=0; }

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
