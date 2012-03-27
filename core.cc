#include "array.cc"

#define array(T) \
template class array<T>; \
/*template array<T> slice(array<T>&& a, uint pos, uint size);*/ \
/*template array<T> slice(array<T>&& a, uint pos);*/ \
/*Copyable*/ \
template array<T> slice(const array<T>& a, uint pos, uint size); \
template array<T> slice(const array<T>& a, uint pos); \
template array<T>& operator <<(array<T>& a, const T& v); \
template array<T>& operator <<(array<T>& a, const array<T>& b); \
template array<T> copy(const array<T>& a); \
/*DefaultConstructible*/ \
template void grow(array<T>& a, uint size); \
template void resize(array<T>& a, uint size); \
/*Comparable*/ \
template bool contains(const array<T>&, const T&); \
template int indexOf(const array<T>& a, const T& value); \
template bool operator ==(const array<T>&, const array<T>&); \
template bool operator !=(const array<T>&, const array<T>&);

array(byte)
array(ubyte)
array(int16)
array(uint16)
array(int)
array(uint)
array(float)
array(int64)
array(uint64)
