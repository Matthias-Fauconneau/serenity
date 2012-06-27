#pragma once

//TODO: change to 8byte granularity (for aligned inline free list (size+next))
typedef unsigned char byte;

byte* allocate_(int size);
byte* reallocate_(byte* buffer, int size, int need);
void unallocate_(byte* buffer, int size);

template<class T> inline T* allocate(int size) { return (T*)allocate_(size*sizeof(T)); }
template<class T> inline T* reallocate(const T* buffer, int size, int need) {
    return (T*)reallocate_((byte*)buffer, sizeof(T)*size,sizeof(T)*need); }
template<class T> inline void unallocate(T* buffer, int size) { unallocate_((byte*)buffer,size*sizeof(T)); }

//raw buffer zero initialization //TODO: SSE
inline void clear(byte* dst, int size) { for(int i=0;i<size;i++) dst[i]=0; }
//unsafe (ignoring constructors) raw value zero initialization
template<class T> inline void clear(T& dst) { static_assert(sizeof(T)>8,""); clear((byte*)&dst,sizeof(T)); }
//safe buffer default initialization
template<class T> inline void clear(T* data, int size, const T& value=T()) { for(int i=0;i<size;i++) data[i]=value; }

//raw buffer copy //TODO: SSE
inline void copy(byte* dst,const byte* src, int size) { for(int i=0;i<size;i++) dst[i]=src[i]; }
//unsafe (ignoring constructors) raw value copy
template<class T> inline void copy(T& dst,const T& src) { copy((byte*)&dst,(byte*)&src,sizeof(T)); }
