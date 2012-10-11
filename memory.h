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
/// Aligned raw memory copy
void copy16(void* dst,const void* src, int size);
/// Buffer explicit copy
template<class T> void copy(T* dst,const T* src, int count) { for(int i=0;i<count;i++) dst[i]=src[i]; }

/// C runtime memory allocation
extern "C" void* malloc(size_t size);
extern "C" int posix_memalign(void** buffer, size_t alignment, size_t size);
extern "C" void* realloc(void* buffer, size_t size);
extern "C" void free(void* buffer);

/// Typed memory allocation (without initialization)
#if HEAP_TRACE
extern void heapTrace(int delta);
template<class T> T* allocate(uint size) { assert(size); heapTrace(size*sizeof(T)); return (T*)malloc(size*sizeof(T)); }
template<class T> T* allocate16(uint size) { void* buffer; heapTrace(size*sizeof(T)); posix_memalign(&buffer,16,size*sizeof(T)); return (T*)buffer; }
template<class T> void reallocate(T*& buffer, int unused size, int need) { heapTrace((need-size)*sizeof(T)); buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer, int unused size) { assert(buffer); heapTrace(-size*sizeof(T)); free((void*)buffer); buffer=0; }
#else
template<class T> T* allocate(uint size) { assert(size); return (T*)malloc(size*sizeof(T)); }
template<class T> T* allocate16(uint size) { void* buffer; posix_memalign(&buffer,16,size*sizeof(T)); return (T*)buffer; }
template<class T> void reallocate(T*& buffer, int unused size, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer, int unused size) { assert(buffer); free((void*)buffer); buffer=0; }
#endif

/// Dynamic object allocation (using constructor and destructor)
inline void* operator new(size_t, void* p) { return p; } //placement new
template<class T, class... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<class T> void free(T* t) { t->~T(); unallocate(t,1); }

/// Unique reference to an heap allocated value
template<class T> struct unique {
    no_copy(unique);
    T* pointer;
    /// Instantiates a new value
    template<class... Args> unique(Args&&... args):pointer(&heap<T>(forward<Args>(args)___)){}
    template<class O> unique(unique<O>&& o){pointer=o.pointer; o.pointer=0;}
    template<class O> unique& operator=(unique<O>&& o){this->~unique(); pointer=o.pointer; o.pointer=0; return *this;}
    ~unique() { if(pointer) free(pointer); }
    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
};
