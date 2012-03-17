#include "array.h"
#include "string.h"

#define generic template<class T>

generic T* array<T>::data() { return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
generic const T* array<T>::data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
generic uint array<T>::size() const { return tag>=0?tag:buffer.size; }
generic void array<T>::setSize(uint size) { assert(size<=capacity()); if(tag>=0) tag=size; else buffer.size=size;}
generic uint array<T>::capacity() const { return tag>=0 ? inline_capacity() : buffer.capacity; }

generic array<T>::array(array<T>&& o) {
    if(o.tag<0) buffer=o.buffer; else copy(*this,o);
    o.tag=0;
}

generic array<T>& array<T>::operator=(array<T>&& o) {
    array::~array();
    if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy((byte*)this,(byte*)&o,sizeof(*this));
    o.tag=0;
    return *this;
}

generic array<T>::array(uint capacity) : tag(0) { reserve(capacity); }
generic array<T>::array(std::initializer_list<T>&& list) {
    reserve(list.size()); setSize(list.size());
    for(uint i=0;i<list.size();i++) new (&at(i)) T(move(((T*)list.begin())[i]));
}
generic array<T>::~array() { if(capacity()) { for(uint i=0;i<size();i++) at(i).~T(); if(tag==-1) unallocate(buffer.data); } }

generic void array<T>::reserve(uint capacity) {
    if(capacity <= array::capacity()) return;
    if(tag==-1 && buffer.capacity) {
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
        tag=-1;
        buffer.capacity=capacity;
    }
}

template<class T, class O> array<T> cast(array<O>&& o) {
    array<T> r;
    if(o.tag<0) {
        r.tag=o.tag;
        r.buffer.data=(const T*)o.buffer.data;
        r.buffer.size = o.buffer.size*sizeof(O)/sizeof(T);
        r.buffer.capacity = o.buffer.capacity*sizeof(O)/sizeof(T);
    } else {
        copy((byte*)&r, (byte*)&o, sizeof(array<T>));
        r.tag = o.tag*sizeof(O)/sizeof(T);
    }
    return r;
}

generic void array<T>::shrink(uint size) { assert(size<array::size()); if(capacity()) for(uint i=size;i<array::size();i++) at(i).~T(); setSize(size); }
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
generic array<T>& array<T>::operator <<(array<T>&& a) { append(move(a)); return *this; }

generic void array<T>::insertAt(int index, T&& v) {
    reserve(size()+1); setSize(size()+1);
    for(int i=size()-2;i>=index;i--) copy(at(i+1),at(i));
    new (&at(index)) T(move(v));
}

#define array array<T>

generic array slice(array&& a, uint pos, uint size) {
    assert(pos+size<=a.size());
    assert(pos==0 || a.capacity() == 0); //only allow slicing of referencing arrays. TODO: custom realloc with slicing
    return array(a.data()+pos,size);
}
generic array slice(array&& a, uint pos) { return slice(move(a),pos,a.size()-pos); }

// Copyable?
generic array slice(const array& a, uint pos, uint size) {
    assert(pos+size<=a.size());
    return copy(array(a.data()+pos,size));
}
generic array slice(const array& a, uint pos) { return slice(a,pos,a.size()-pos); }
generic array& operator <<(array& a, const T& v) { a.append(copy(v)); return a; }
generic array& operator <<(array& a, const array& b) {
    int old=a.size(); a.reserve(old+b.size()); a.setSize(old+b.size());
    for(uint i=0;i<b.size();i++) new (&a.at(old+i)) T(copy(b[i]));
    return a;
}
generic array copy(const array& a) { array r(a.size()); r.setSize(a.size()); for(uint i=0;i<a.size();i++) new (&r.at(i)) T(copy(a[i])); return  r; }

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
generic void removeOne(array& a, T v) { int i=indexOf(a, v); if(i>=0) a.removeAt(i); }
generic void appendOnce(array& a, T&& v) { if(!contains(a,v)) a.append(move(v)); }
generic void appendOnce(array& a, const T& v) { if(!contains(a,v)) a.append(copy(v)); }
generic array replace(array&& a, const T& before, const T& after) {
        for(auto& e : a) if(e==before) e=copy(after); return move(a);
}

// Orderable?
generic const T& min(const array& a) { T* min=&a.first(); for(T& e: a) if(e<*min) min=&e; return *min; }
generic T& max(array& a) { T* max=&a.first(); for(T& e: a) if(e>*max) max=&e; return *max; }
//generic T sum(const array& a) { T sum=0; for(const T& e: a) sum+=e; return sum; }

#undef generic
#undef array
