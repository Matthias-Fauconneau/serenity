#include "array.h"
#include "debug.h"
#include "memory.h"

inline void* operator new(size_t, void* p) { return p; } //placement new

#define generic template<class T>
generic void array<T>::setSize(uint size) { assert(size<=capacity()); if(tag>=0) tag=size; else buffer.size=size;}
generic uint array<T>::capacity() const { return tag>=0 ? inline_capacity() : buffer.capacity; }

generic array<T>::array(array<T>&& o) {
    if(o.tag<0) buffer=o.buffer; else copy(*this,o);
    o.tag=0;
}

generic array<T>& array<T>::operator=(array<T>&& o) {
    this->~array();
    if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy((byte*)this,(byte*)&o,sizeof(*this));
    o.tag=0;
    return *this;
}

generic array<T>::array(uint capacity) { reserve(capacity); }
generic array<T>::array(initializer_list<T>&& list) {
    reserve(list.size()); setSize(list.size());
    for(uint i=0;i<list.size();i++) new (&at(i)) T(move(((T*)list.begin())[i]));
}

generic void array<T>::reserve(uint capacity) {
    if(capacity <= array::capacity()) return;
    if(tag==-2 && buffer.capacity) {
        buffer.data=reallocate<T>(buffer.data,buffer.capacity,capacity);
        buffer.capacity=capacity;
    } else if(capacity <= inline_capacity()) {
        if(tag<0) {
             tag=buffer.size;
             copy((byte*)(&tag+1),(byte*)buffer.data,buffer.size*sizeof(T));
        }
    } else {
        T* heap=allocate<T>(capacity);
        copy((byte*)heap,(byte*)data(),size()*sizeof(T));
        buffer.data=heap;
        if(tag>=0) buffer.size=tag;
        tag=-2;
        buffer.capacity=capacity;
    }
}

generic void array<T>::shrink(uint size) {
    assert(size<array::size());
    if(tag!=-1) for(uint i=size;i<array::size();i++) at(i).~T();
    setSize(size);
}
generic void array<T>::clear() { if(size()) shrink(0); }

generic void array<T>::removeAt(uint i) {
    at(i).~T();
    for(;i<size()-1;i++) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T));
    setSize(size()-1);
}
generic void array<T>::removeLast() { assert(size()!=0); last().~T(); setSize(size()-1); }
generic T array<T>::take(int i) { T value = move(at(i)); removeAt(i); return value; }
generic T array<T>::takeFirst() { return take(0); }
generic T array<T>::takeLast() { return take(size()-1); }
generic T array<T>::pop() { return takeLast(); }

generic void array<T>::append(T&& v) { int s=size()+1; reserve(s); new (end()) T(move(v)); setSize(s); }
generic array<T>& array<T>::operator <<(T&& v) { append(move(v)); return *this; }
generic void array<T>::append(array&& a) { int s=size()+a.size(); reserve(s); copy((byte*)end(),(byte*)a.data(),a.size()*sizeof(T)); setSize(s); }
generic array<T>& array<T>::operator <<(array<T>&& a) { int s=size()+a.size(); reserve(s); copy((byte*)end(),(byte*)a.data(),a.size()*sizeof(T)); setSize(s); return *this; }

#define array array<T>

// Copyable?
generic array slice(const array& a, uint pos, uint size) {
    assert(pos+size<=a.size());
    return copy(array(a.data()+pos,size));
}
generic array slice(const array& a, uint pos) { return slice(a,pos,a.size()-pos); }
generic array& operator <<(array& a, T const& v) { a.append(copy(v)); return a; }
generic array& operator <<(array& a, const array& b) {
    int old=a.size(); a.reserve(old+b.size()); a.setSize(old+b.size());
    for(uint i=0;i<b.size();i++) new (&a.at(old+i)) T(copy(b[i]));
    return a;
}
generic array copy(const array& a) { array r(a.size()); r.setSize(a.size()); for(uint i=0;i<a.size();i++) new (&r.at(i)) T(copy(a[i])); return  r; }

generic T& insertAt(array& a, int index, T&& v) {
    a.reserve(a.size()+1); a.setSize(a.size()+1);
    for(int i=a.size()-2;i>=index;i--) copy(a.at(i+1),a.at(i));
    new (&a.at(index)) T(move(v));
    return a.at(index);
}

generic T& insertAt(array& a, int index, const T& v) {
    a.reserve(a.size()+1); a.setSize(a.size()+1);
    for(int i=a.size()-2;i>=index;i--) copy(a.at(i+1),a.at(i));
    new (&a.at(index)) T(copy(v));
    return a.at(index);
}

// DefaultConstructible?
generic void grow(array& a, uint size) { uint old=a.size(); assert(size>old); a.reserve(size); a.setSize(size); for(uint i=old;i<size;i++) new (&a.at(i)) T(); }
generic void resize(array& a, uint size) { if(size<a.size()) a.shrink(size); else if(size>a.size()) grow(a, size); }

// Comparable?
generic bool operator ==(const array& a, const array& b) {
    if(a.size() != b.size()) return false;
    for(uint i=0;i<a.size();i++) if(!(a[i]==b[i])) return false;
    return true;
}
generic bool operator !=(const array& a, const array& b) { return !(a==b); }
generic int indexOf(const array& a, const T& value) { for(uint i=0;i<a.size();i++) { if(a[i]==value) return i; } return -1; }
generic bool contains(const array& a, const T& value) { return indexOf(a,value)>=0; }
generic int removeOne(array& a, T v) { int i=indexOf(a, v); if(i>=0) a.removeAt(i); return i; }
generic void removeAll(array& a, T v) { for(uint i=0;i<a.size();i++) if(a[i]==v) a.removeAt(i), i--; }
generic void appendOnce(array& a, T&& v) { if(!contains(a,v)) a.append(move(v)); }
generic void appendOnce(array& a, const T& v) { if(!contains(a,v)) a.append(copy(v)); }
generic array replace(array&& a, const T& before, const T& after) {
        for(T& e : a) if(e==before) e=copy(after); return move(a);
}

// Orderable?
generic const T& min(const array& a) { T* min=&a.first(); for(T& e: a) if(e<*min) min=&e; return *min; }
generic T& max(array& a) { T* max=&a.first(); for(T& e: a) if(e>*max) max=&e; return *max; }
generic int insertSorted(array& a, T&& v) { uint i=0; for(;i<a.size();i++) if(a[i] > v) break; insertAt(a, i,move(v)); return i; }
generic int insertSorted(array& a, const T& v) { return insertSorted(a,copy(v)); }

#undef generic
#undef array

/// Convenience macros for explicit template instanciation

#define Array(T) template struct array< T >; \
template T& insertAt(array<T>& a, int index, T&& v);
#define Copy(T) \
template array<T> slice(const array<T>& a, uint pos, uint size); \
template array<T> slice(const array<T>& a, uint pos); \
template array<T>& operator <<(array<T>& a, T const& v); \
template array<T>& operator <<(array<T>& a, const array<T>& b); \
template array<T> copy(const array<T>& a); \
template T& insertAt(array<T>& a, int index, T const& v);
#define Default(T) \
template void grow(array<T>& a, uint size); \
template void resize(array<T>& a, uint size);
#define Compare(T) \
template bool contains(const array<T>&, T const&); \
template int indexOf(const array<T>& a, T const& value); \
template bool operator ==(const array<T>&, const array<T>&); \
template bool operator !=(const array<T>&, const array<T>&); \
template int removeOne(array<T>& a, T v);
#define Sort(T) \
template int insertSorted(array<T>& a, T&& v); \
template int insertSorted(array<T>& a, T const& v);
#define Array_Copy(T) Array(T) Copy(T)
#define Array_Compare(T) Array(T) Compare(T)
#define Array_Default(T) Array(T) Default(T)
#define Array_Copy_Compare(T) Array(T) Copy(T) Compare(T)
#define Array_Copy_Compare_Sort(T) Array(T) Copy(T) Compare(T) Sort(T)
#define Array_Copy_Compare_Sort_Default(T) Array(T) Copy(T) Default(T) Compare(T) Sort(T)
