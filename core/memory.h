#pragma once
/// \file memory.h Memory operations and management (mref, buffer, unique, shared)
#include "core.h"

// C runtime memory allocation
extern "C" void* malloc(size_t size) noexcept;
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size) noexcept;
extern "C" void* realloc(void* buffer, size_t size) noexcept;
extern "C" void free(void* buffer) noexcept;

/// Managed fixed-capacity mutable reference to an array of elements
/// \note either an heap allocation managed by this object or a reference to memory managed by another object
/// \note Use array for objects with move constructors as buffer elements are not initialized on allocation
generic struct buffer : mref<T> {
    using mref<T>::data;
    using mref<T>::size;
    size_t capacity = 0; /// 0: reference, >0: size of the owned heap allocation
    debug(static bool& logMemory() { static bool value = true; return value; })

    /// Default constructs an empty buffer
    buffer(){}
    /// References \a size elements from const \a data pointer
    buffer(T* data, size_t size) : mref<T>(data, size) {}
    /// Move constructor
    buffer(buffer&& o) : mref<T>(o), capacity(o.capacity) {o.data=0, o.size=0, o.capacity=0; }
    /// Allocates an uninitialized buffer for \a capacity elements
    buffer(size_t capacity, size_t size) : mref<T>((T*)0,size), capacity(capacity) {
        assert(capacity>=size && size>=0); if(!capacity) return;
        if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error("Out of memory");
        debug(if(logMemory()) { logMemory()=false; log("+", capacity,'\t', size,'\t', (void*)data); logMemory()=true; })
    }
    explicit buffer(size_t size) : buffer(size, size){}
    /// Allocates a buffer for \a capacity elements and fill with value
    template<Type Arg, Type... Args> buffer(size_t capacity, size_t size, Arg arg, Args&&... args) : buffer(capacity, size) { this->clear(arg, args...); }
    /// Initializes a new buffer with the content of \a o
    explicit buffer(const ref<T> o) : buffer(o.size) { mref<T>::copy(o); }

    buffer& operator=(buffer&& o) { this->~buffer(); new (this) buffer(::move(o)); return *this; }
    /// If the buffer owns the reference, returns the memory to the allocator
    ~buffer() {
#if DEBUG
        if(logMemory() && capacity) {
            logMemory()=false; log("~", capacity,'\t', size,'\t', (void*)data); logMemory()=true;
        }
#endif
        if(capacity) ::free((void*)data); data=0; capacity=0; size=0;
    }
};
/// Initializes a new buffer with the content of \a o
generic buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity?:o.size, o.size); t.copy(o); return t; }

/// Converts a reference to a buffer (unsafe as no reference counting will keep the original buffer from being freed)
generic buffer<T> unsafeReference(const ref<T> o) { return buffer<T>((T*)o.data, o.size); }

/*/// Concatenates a single element and a buffer by copying
generic inline buffer<T> operator+(const T a, const ref<T> b) {
    buffer<T> target(1+b.size); target.set(0, a); target.slice(1).copy(b); return target;
}

/// Concatenates a buffer and a single element by copying
generic inline buffer<T> operator+(const ref<T> a, T b) {
    buffer<T> target(a.size+1); target.slice(0, a.size).copy(a); target.set(a.size, b); return target;
}

/// Concatenates two buffers by copying
generic inline buffer<T> operator+(const ref<T> a, const ref<T> b) {
    buffer<T> target(a.size+b.size); target.slice(0, a.size).copy(a); target.slice(a.size).copy(b); return target;
}*/

/// Returns an array of the application of a function to every index up to a size
template<Type Function> auto apply(size_t size, Function function) -> buffer<decltype(function(0))> {
    buffer<decltype(function(0))> target(size); target.apply(function); return target;
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type T> auto apply(ref<T> source, Function function) -> buffer<decltype(function(source[0]))> {
    buffer<decltype(function(source[0]))> target(source.size); target.apply(function, source); return target;
}

// -- Index operations: floor / align

/// Aligns \a offset down to previous \a width wide step (only for power of two \a width)
inline uint floor(uint width, uint offset) { assert((width&(width-1))==0); return offset & ~(width-1); }
/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

// -- Reinterpret casts

/// Reinterpret casts a const reference to another type
template<Type T, Type O> ref<T> cast(const ref<O> o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret casts a mutable reference to another type
template<Type T, Type O> mref<T> mcast(const mref<O>& o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return mref<T>((T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret casts a buffer to another type
template<Type T, Type O> buffer<T> cast(buffer<O>&& o) {
    buffer<T> buffer;
    buffer.data = (const T*)o.data;
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    buffer.size = o.size*sizeof(O)/sizeof(T);
    assert((o.capacity*sizeof(O))%sizeof(T) == 0);
    buffer.capacity = o.capacity*sizeof(O)/sizeof(T);
    o.capacity = 0;
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
