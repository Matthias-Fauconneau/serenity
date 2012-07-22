#pragma once
typedef unsigned char byte;

byte* allocate_(int size);
byte* reallocate_(byte* buffer, int size, int need);
void unallocate_(byte* buffer, int size);

template<class T> inline T* allocate(int size) { return (T*)allocate_(size*sizeof(T)); }
template<class T> inline T* reallocate(const T* buffer, int size, int need) { return (T*)reallocate_((byte*)buffer, sizeof(T)*size,sizeof(T)*need); }
template<class T> inline void unallocate(T*& buffer, int size) { unallocate_((byte*)buffer,size*sizeof(T)); buffer=0; }

inline void* operator new(unsigned int, void* p) { return p; } //placement new
template<class T, class... Args> inline T& alloc(Args&&... args) { T* t=allocate<T>(1); new (t) T(move(args)...); return *t; }
template<class T> inline void free(T* t) { unallocate(t,1); }
