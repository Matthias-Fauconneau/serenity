#pragma once
#include "core.h"

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

extern "C" void* malloc(unsigned long size);
extern "C" int posix_memalign(void** buffer, unsigned long alignment, unsigned long size);
extern "C" void* realloc(void* buffer, unsigned long size);
extern "C" void free(void* buffer);

template<class T> T* allocate(int size) { assert_(size); return (T*)malloc(size*sizeof(T)); }
template<class T> T* allocate16(int size) { void* buffer; posix_memalign(&buffer,16,size*sizeof(T)); return (T*)buffer; }
template<class T> void reallocate(T*& buffer, int unused size, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer, int unused size) { assert_(buffer); free((void*)buffer); buffer=0; }

template<class T, class... Args> T& heap(Args&&... args) { T* t=allocate16<T>(1); new (t) T(forward<Args>(args)___); return *t; }
template<class T> void free(T* t) { t->~T(); unallocate(t,1); }

/// Unique reference to an heap allocated value
template<class T> struct unique {
    no_copy(unique)
    T* pointer;
    template<class... Args> unique(Args&&... args):pointer(&heap<T>(forward<Args>(args)___)){}
    unique(unique&& o){pointer=o.pointer; o.pointer=0;}
    ~unique() { if(pointer) free(pointer); }
    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
};
