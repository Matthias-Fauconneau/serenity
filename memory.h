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
template<class T> void reallocate(T*& buffer, int unused size, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer, int unused size) { assert(buffer); free((void*)buffer); buffer=0; }

/// Dynamic object allocation (using constructor and destructor)
inline void* operator new(size_t, void* p) { return p; } //placement new
template<class T, class... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<class T> void free(T* t) { t->~T(); unallocate(t,1); }

/// Unique reference to an heap allocated value
template<class T> struct c {
    no_copy(unique);
    T* pointer;
    unique():pointer(0){}
    template<class O> unique(unique<O>&& o){pointer=o.pointer; o.pointer=0;}
    template<class O> unique& operator=(unique<O>&& o){this->~unique(); pointer=o.pointer; o.pointer=0; return *this;}
    /// Instantiates a new value
    template<class... Args> unique(Args&&... args):pointer(&heap<T>(forward<Args>(args)___)){}
    ~unique() { if(pointer) free(pointer); }
    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
    explicit operator bool() { return pointer; }
    bool operator !() const { return !pointer; }
};
