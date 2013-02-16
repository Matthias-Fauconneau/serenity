#pragma once
/// \file memory.h Memory operations (clear, copy, heap allocate, unique heap reference)
#include "core.h"

/// References raw memory representation of \a t
template<class T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// Raw zero initialization
inline void clear(byte* buffer, int size) { for(int i=0;i<size;i++) buffer[i]=0; }
/// Buffer default initialization
template<class T> void clear(T* buffer, int size, const T& value=T()) { for(int i=0;i<size;i++) buffer[i]=value; }

/// Raw memory copy
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
/// Buffer explicit copy
template<class T> void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=src[i]; }

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);
// Typed memory allocation (without initialization)
template<class T> T* allocate(uint size) { assert(size); return (T*)malloc(size*sizeof(T)); }
template<class T> T* allocate64(uint size) { void* buffer; if(posix_memalign(&buffer,64,size*sizeof(T))) error(""); return (T*)buffer; }
template<class T> void reallocate(T*& buffer, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer) { assert(buffer); free((void*)buffer); buffer=0; }

/// Dynamic object allocation (using constructor and destructor)
inline void* operator new(size_t, void* p) { return p; } //placement new
template<class T, class... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<class T> void free(T* t) { t->~T(); unallocate(t); }

/// Simple writable fixed-capacity memory reference
template<class T> struct buffer {
    T* data=0;
    uint capacity=0,size=0;
    buffer(){}
    explicit buffer(uint capacity):data(allocate64<T>(capacity)),capacity(capacity){}
    buffer(uint size, const T& value):data(allocate64<T>(size)),capacity(size),size(size){for(T& e: ref<T>(data,size)) e=value;}
    buffer(const buffer& o):buffer(o.capacity){size=o.size; copy(data,o.data,size*sizeof(T));}
    move_operator_(buffer):data(o.data),capacity(o.capacity),size(o.size){o.data=0;}
    ~buffer(){if(data){unallocate(data);}}
    operator T*() { return data; }
    operator ref<T>() const { return ref<T>(data,size); }
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator[](uint i) const { assert(i<size); return data[i]; }
    T& operator[](uint i) { assert(i<size); return (T&)data[i]; }
};
