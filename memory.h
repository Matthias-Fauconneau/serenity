#pragma once
/// \file memory.h Memory operations (clear, copy, heap allocate, unique heap reference)
#include "core.h"

/// References raw memory representation of \a t
template<Type T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }

/// Raw zero initialization
inline void clear(byte* buffer, uint size) { for(uint i: range(size)) buffer[i]=0; }
/// Buffer default initialization
template<Type T> void clear(T* buffer, uint size, const T& value=T()) { for(uint i: range(size)) buffer[i]=value; }

/// Raw memory copy
inline void copy(byte* dst,const byte* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }
/// Buffer explicit copy
template<Type T> void copy(T* dst,const T* src, uint size) { for(uint i: range(size)) dst[i]=src[i]; }

// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);
// Typed memory allocation (without initialization)
template<Type T> T* allocate(uint size) { assert(size); return (T*)malloc(size*sizeof(T)); }
template<Type T> T* allocate64(uint size) { void* buffer; if(posix_memalign(&buffer,64,size*sizeof(T))) error(""); return (T*)buffer; }
template<Type T> void reallocate(T*& buffer, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<Type T> void unallocate(T*& buffer) { assert(buffer); free((void*)buffer); buffer=0; }

/// Dynamic object allocation (using constructor and destructor)
inline void* operator new(size_t, void* p) { return p; } //placement new
template<Type T, Type... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<Type T> void free(T* t) { t->~T(); unallocate(t); }

/// Simple writable fixed-capacity memory reference
template<Type T> struct buffer {
    T* data=0;
    uint capacity=0,size=0;
    buffer(){}
    explicit buffer(uint capacity):data(allocate64<T>(capacity)),capacity(capacity){}
    buffer(uint size, const T& value):data(allocate64<T>(size)),capacity(size),size(size){clear(data,size,value);}
    move_operator_(buffer):data(o.data),capacity(o.capacity),size(o.size){o.data=0;}
    ~buffer(){if(data){unallocate(data);}}
    explicit operator bool() const { return data; }
    operator T*() { return data; }
    operator ref<T>() const { return ref<T>(data,size); }
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator[](uint i) const { assert(i<size); return data[i]; }
    T& operator[](uint i) { assert(i<size); return (T&)data[i]; }
};
template<Type T> inline buffer<T> copy(const buffer<T>& o){buffer<T> t(o.capacity); t.size=o.size; copy(t.data,o.data,o.size); return t; }
