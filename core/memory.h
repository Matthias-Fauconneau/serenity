#pragma once
/// \file memory.h Memory operations and management (mref, buffer, unique, shared)
#include "core.h"

/// Initializes memory using a constructor (placement new)
inline void* operator new(size_t, void* p) throw() { return p; }

/// Unmanaged fixed-size mutable reference to an array of elements
generic struct mref : ref<T> {
    /// Default constructs an empty reference
    mref(){}
    /// References \a size elements from \a data pointer
    mref(T* data, size_t size) : ref<T>(data,size) {}
    /// Converts an std::initializer_list to mref
    constexpr mref(std::initializer_list<T>&& list) : ref<T>(list.begin(), list.size()) {}
    /// Converts a static array to ref
    template<size_t N> mref(T (&a)[N]): mref(a,N) {}

    explicit operator bool() const { if(size) assert(data); return size; }
    explicit operator T*() const { return (T*)data; }
    T* begin() const { return (T*)data; }
    T* end() const { return (T*)data+size; }
    T& at(size_t i) const { assert(i<size); return (T&)data[i]; }
    T& operator [](size_t i) const { return at(i); }
    T& first() const { return at(0); }
    T& last() const { return at(size-1); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    mref<T> slice(size_t pos, size_t size) const { assert(pos+size<=this->size); return mref<T>((T*)data+pos, size); }
    /// Slices a reference to elements from to the end of the reference
    mref<T> slice(size_t pos) const { assert(pos<=size); return mref<T>((T*)data+pos,size-pos); }

    /// Initializes buffer using the same constructor for all elements
    template<Type... Args> void clear(Args... args) const { for(size_t i: range(size)) new (&at(i)) T(args...); }

    using ref<T>::data;
    using ref<T>::size;
};
/// Returns mutable reference to memory used by \a t
generic mref<byte> raw(T& t) { return mref<byte>((byte*)&t,sizeof(T)); }

// Memory operations
/// Initializes \a dst from \a src using move constructor
generic void move(const mref<T>& dst, const mref<T>& src) { assert(dst.size==src.size); for(size_t i: range(src.size)) new(&dst[i]) T(move(src[i])); }
/// Initializes \a dst from \a src using copy constructor
generic void copy(const mref<T>& dst, const ref<T> src) { assert(dst.size==src.size); for(size_t i: range(src.size)) new(&dst[i]) T(copy(src[i])); }

// C runtime memory allocation
extern "C" void* malloc(size_t size) throw();
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size) throw();
extern "C" void* realloc(void* buffer, size_t size) throw();
extern "C" void free(void* buffer) throw();

/// Managed fixed-capacity mutable reference to an array of elements
/// \note either an heap allocation managed by this object or a reference to memory managed by another object
/// \note Use array for objects with move constructors as buffer elements are not initialized on allocation
generic struct buffer : mref<T> {
    /// Default constructs an empty buffer
    buffer(){}
    /// References \a size elements from const \a data pointer
    buffer(const T* data, size_t size) : mref<T>((T*)data, size) {}
    /// References \a o.size elements from \a o.data pointer
    explicit buffer(const ref<T>& o): mref<T>((T*)o.data, o.size) {}
    /// Move constructor
    buffer(buffer&& o) : mref<T>((T*)o.data, o.size), capacity(o.capacity) {o.data=0, o.size=0, o.capacity=0; }
    /// Allocates an uninitialized buffer for \a capacity elements
    buffer(size_t capacity, size_t size):mref<T>((T*)0,size),capacity(capacity){
     assert(capacity>=size && size>=0); if(!capacity) return;
     if(posix_memalign((void**)&data,64,capacity*sizeof(T))) error("");
    }
    explicit buffer(size_t size) : buffer(size, size){}
    /// Allocates a buffer for \a capacity elements and fill with value
    template<Type Arg, Type... Args> buffer(size_t capacity, size_t size, Arg arg, Args&&... args) : buffer(capacity, size) { this->clear(arg, args...); }

    buffer& operator=(buffer&& o){ this->~buffer(); new (this) buffer(move(o)); return *this; }
    /// If the buffer owns the reference, returns the memory to the allocator
    ~buffer() { if(capacity) ::free((void*)data); data=0; capacity=0; size=0; }

    using mref<T>::data;
    using mref<T>::size;
    size_t capacity=0; /// 0: reference, >0: size of the owned heap allocation
};
/// Initializes a new buffer with the content of \a o
generic buffer<T> copy(const buffer<T>& o){ buffer<T> t(o.capacity?:o.size, o.size); for(uint i: range(o.size)) new (&t[i]) T(copy(o[i])); return t; }
/// Converts a reference to a buffer (unsafe as no reference counting will keep the original buffer from being freed)
generic buffer<T> unsafeReference(const ref<T>& o) { return buffer<T>(o.data, o.size); }

/// Unique reference to an heap allocated value
generic struct unique {
    unique(decltype(nullptr)):pointer(0){}
    template<Type D> unique(unique<D>&& o):pointer(o.pointer){o.pointer=0;}
    template<Type... Args> explicit unique(Args&&... args):pointer(new T(forward<Args>(args)...)){}
    unique& operator=(unique&& o){ this->~unique(); new (this) unique(move(o)); return *this; }
    ~unique() { if(pointer) { delete pointer; } pointer=0; }

    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    explicit operator bool() const { return pointer; }
    bool operator !() const { return !pointer; }
    bool operator ==(const unique<T>& o) const { return pointer==o.pointer; }

    T* pointer;
};
generic unique<T> copy(const unique<T>& o) { return unique<T>(copy(*o.pointer)); }

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
