#pragma once
#include "core.h"

extern "C" void* malloc(unsigned long size);
extern "C" void* realloc(void* buffer, unsigned long size);
extern "C" void free(void* buffer);

template<class T> T* allocate(int size) { assert_(size); return (T*)malloc(size*sizeof(T)); }
template<class T> void reallocate(T*& buffer, int unused size, int need) { buffer=(T*)realloc((void*)buffer, need*sizeof(T)); }
template<class T> void unallocate(T*& buffer, int unused size) { assert_(buffer); free((void*)buffer); buffer=0; }

template<class T, class... Args> T& heap(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)___); return *t; }
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
