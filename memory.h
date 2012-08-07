#pragma once
#include "core.h"

byte* allocate_(uint size);
byte* reallocate_(byte* buffer, int size, int need);
void unallocate_(byte* buffer, int size);

template<class T> inline T* allocate(int size) { return (T*)allocate_(size*sizeof(T)); }
template<class T> inline T* reallocate(const T* buffer, int size, int need) { return (T*)reallocate_((byte*)buffer, size*sizeof(T), need*sizeof(T)); }
template<class T> inline void unallocate(T*& buffer, int size) { unallocate_((byte*)buffer,size*sizeof(T)); buffer=0; }

template<class T, class... Args> inline T& alloc(Args&&... args) { T* t=allocate<T>(1); new (t) T(forward<Args>(args)...); return *t; }
template<class T> inline void free(T* t) { t->~T(); unallocate(t,1); }

/// Unique reference to an heap allocated value
template<class T> struct unique {
    no_copy(unique)
    T* pointer;
    template<class... Args> unique(Args&&... args):pointer(&alloc<T>(forward<Args>(args)...)){}
    unique(unique&& o){pointer=o.pointer; o.pointer=0;}
    ~unique() { if(pointer) free(pointer); }
    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
};
