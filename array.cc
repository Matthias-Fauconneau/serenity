#include "array.h"
#include "debug.h"
#include "memory.h"
inline void* operator new(size_t, void* p) { return p; } //placement new

#define array array<T>

generic void array::reserve(uint capacity) {
    if(capacity <= this->capacity()) return;
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

generic void array::shrink(uint size) {
    assert(size<this->size());
    if(tag!=-1) for(uint i=size;i<this->size();i++) at(i).~T();
    setSize(size);
}
generic void array::clear() { if(size()) shrink(0); }

generic void array::removeAt(uint i) {
    at(i).~T();
    for(;i<size()-1;i++) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T));
    setSize(size()-1);
}
generic void array::removeLast() { assert(size()!=0); last().~T(); setSize(size()-1); }
generic T array::take(int i) { T value = move(at(i)); removeAt(i); return value; }
generic T array::takeFirst() { return take(0); }
generic T array::takeLast() { return take(size()-1); }
generic T array::pop() { return takeLast(); }

generic array& operator <<(array& a, T&& v) { int s=a.size()+1; a.reserve(s); new (a.end()) T(move(v)); a.setSize(s); return a; }
generic array& operator <<(array& a, array&& b) {
    int s=a.size()+b.size(); a.reserve(s); copy((byte*)a.end(),(byte*)b.data(),b.size()*sizeof(T)); a.setSize(s); return a;
}

// Copyable?
generic array slice(const array& a, uint pos, uint size) {
    assert(pos+size<=a.size());
    return copy(array(a.data()+pos,size));
}
generic array slice(const array& a, uint pos) { return slice(a,pos,a.size()-pos); }
generic array& operator <<(array& a, T const& v) { a<< copy(v); return a; }
generic array& operator <<(array& a, const ref<T>& b) {
    int old=a.size(); a.reserve(old+b.size); a.setSize(old+b.size);
    for(uint i=0;i<b.size;i++) new (&a.at(old+i)) T(copy(b[i]));
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
generic bool operator ==(const ref<T>& a, const ref<T>& b) {
   if(a.size != b.size) return false;
   for(uint i=0;i<a.size;i++) if(!(a[i]==b[i])) return false;
   return true;
}
generic int indexOf(const ref<T>& a, const T& value) { for(uint i=0;i<a.size;i++) { if(a[i]==value) return i; } return -1; }
generic bool contains(const ref<T>& a, const T& value) { return indexOf(a,value)>=0; }
generic int removeOne(array& a, T v) { int i=indexOf(a, v); if(i>=0) a.removeAt(i); return i; }
generic void removeAll(array& a, T v) { for(uint i=0;i<a.size();i++) if(a[i]==v) a.removeAt(i), i--; }
generic array& operator +=(array& a, const T& v) { if(!contains(a,v)) a<< copy(v); return a; }
generic array& operator +=(array& a, const array& o) { for(const T& v: o) a+= v; return a; }

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
