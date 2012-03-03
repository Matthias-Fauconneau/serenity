#pragma once
#include "core.h"
#include <initializer_list>

/// Returns the next \a offset aligned to \a width
template<int width> inline uint64 align(uint64 offset) {
    static_assert(width && !(width & (width - 1)),"width must be a power of two");
    return (offset + width - 1) & ~(width - 1);
}

/// \a Buffer is a lightweight handle to memory
template <class T> struct Buffer {
    const T* data = 0;
    int size = 0;
    int capacity = 0; //0 = not owned
    Buffer(const T* data=0, int size=0, int capacity=0):data(data),size(size),capacity(capacity){}
};

/// \a array is a typed and bound-checked handle to a memory buffer using move semantics to avoid reference counting
/// It also transparently store small arrays inline when possible (FIXME: not working)
template <class T> struct array {
//read:
    //int8 tag=-1; //allow inline array (without heap allocation), -1 = heap allocated
    Buffer<T> buffer;
    /*T* data() { assert_(tag>=0 || buffer.capacity); return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
    const T* data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
    int size() const { assert_((tag>=0 ? tag : buffer.size)>=0 && (tag>=0 ? tag : buffer.size)<4096); return tag>=0 ? tag : buffer.size; }
    void setSize(int size) { if(tag>=0) tag=size; else buffer.size=size; assert_(array::size()>=0 && array::size()<4096); }
    const int inline_capacity = (sizeof(array)-1)/sizeof(T);
    int capacity() const { return tag>=0 ? inline_capacity : buffer.capacity; }*/
    T* data() { /*assert_(tag>=0 || buffer.capacity);*/ return (T*)buffer.data; }
    const T* data() const { return buffer.data; }
    int size() const { return buffer.size; }
    void setSize(int size) { buffer.size=size; }
    int capacity() const { return buffer.capacity; }

    /// Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    /// \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array)

    /// Default constructs an empty array
    array()/*:tag(0)*/{}

//acquiring constructors
    /// Move constructor
    array(array&& o) { /*if(o.tag==-1)*/ buffer=o.buffer; /*else copy(*this,o); o.tag=0;*/o.buffer.capacity=0; }
    /// Move constructor with conversion
    template <class O> explicit array(array<O>&& o)
        : buffer((const T*)&o, o.size*sizeof(O)/sizeof(T), o.capacity*sizeof(O)/sizeof(T)) { /*assert_(o.tag==-1); o.tag=0;*/o.buffer.capacity=0; }
    /// Move assigment
    array& operator=(array&& o) { /*assert_(o.tag==-1);*/ this->~array(); buffer=o.buffer; /*o.tag=0;*/o.buffer.capacity=0; return *this; }

    /// Allocates a new uninitialized array for \a capacity elements
    explicit array(int capacity) : buffer(allocate<T>(capacity), 0, capacity) { assert_(capacity>0); }
    /// Allocates a new array with \a size elements initialized to \a value
    array(int size, const T& value) : buffer(allocate<T>(size), size, size) { for(int i=0;i<size;i++) new ((void*)(data()+i)) T(value); }
    //// Copy elements from an initializer \a list
    //array(const std::initializer_list<T>& list) { append(array((T*)list.begin(),list.size())); }

//referencing constructors
    /// References \a size elements from read-only \a data pointer
    array(const T* data, int size) : buffer(data, size, 0) { assert_(size>=0); assert_(data); }
    /// References elements sliced from \a begin to \a end
    array(const T* begin,const T* end) : buffer(begin, int(end-begin), 0) { assert_(buffer.size>=0); assert_(begin); }
    ///// References \a size elements from mutable \a buffer with total \a capacity
    //array(T* buffer, int size, int capacity) : data(buffer), size(size), capacity(capacity) { assert_(size<=capacity); assert_(data); }
    /// Reference elements from an initializer \a list
    array(const std::initializer_list<T>& list) : buffer(list.begin(), list.size(), 0) {}

    /// if the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(capacity()) { for(int i=0;i<size();i++) at(i).~T(); /*if(tag==-1)*/ unallocate(buffer.data); } }

    /// Allocates enough memory for \a capacity elements
    void reserve(int capacity) {
        if(array::capacity()>=capacity) return;
        if(/*tag==-1 &&*/ buffer.capacity) {
            buffer.data=reallocate<T>(buffer.data,buffer.capacity,capacity);
            buffer.capacity=capacity;
        } /*else if(capacity <= inline_capacity) {
            assert_(tag>=0); //TODO: free heap
        }*/ else {
            T* heap=allocate<T>(capacity);
            copy((byte*)heap,(byte*)data(),size()*sizeof(T));
            buffer.data=heap;
            //if(tag>=0) { buffer.size=tag; tag=-1; }
            buffer.capacity=capacity;
        }
    }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(int size) { assert_(size<array::size()); if(capacity()) for(int i=size;i<array::size();i++) at(i).~T(); setSize(size); }
    /// Allocates memory for \a size elements and initializes added elements with their default constructor
    void grow(int size) { assert_(size>array::size()); reserve(size); for(int i=array::size();i<size;i++) new ((void*)(data()+i)) T(); setSize(size); }
    /// Sets the array size to \a size, destroying or initializing elements as needed
    void resize(int size) { if(size<array::size()) shrink(size); else if(size>array::size()) grow(size); }
    /// Sets the array size to 0, destroying any contained elements
    void clear() { if(size()) shrink(0); }

    /// Returns a raw pointer to \a data buffer
    T const * operator &() const { return data(); }
    /// Returns true if not empty
    explicit operator bool() const { return size(); }

    /// Accessors
    const T& at(int i) const { assert_(i>=0 && i<size()); return data()[i]; }
    T& at(int i) { assert_(i>=0 && i<size()); return (T&)data()[i]; }
    const T& operator [](int i) const { return at(i); }
    T& operator [](int i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size()-1); }
    T& last() { return at(size()-1); }

    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& value) const { for(int i=0;i<size();i++) { if(at(i)==value) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& value) const { return indexOf(value)>=0; }

    /// remove element
    void removeAt(int i) { assert_(i>=0 && i<size()); at(i).~T(); for(;i<size()-1;i++) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); setSize(size()-1); }
    //void removeRef(T* p) { assert_(p>=data && p<data+size); removeAt(p-data); }
    void removeLast() { assert_(size()); last().~T(); size()--; }
    void removeOne(T v) { int i=indexOf(v); if(i>=0) removeAt(i); }
    T take(int i) { T value = move(at(i)); removeAt(i); return value; }
    T takeFirst() { return take(0); }
    T takeLast() { return take(size-1); }
    T pop() { return takeLast(); }

    /// append element
    void append(T&& v) { int s=size()+1; reserve(s); new (end()) T(move(v)); setSize(s); }
    void append(const T& v) { int s=size()+1; reserve(s); new (end()) T(copy(v)); setSize(s); }
    template<perfect(T)> array& operator <<(Tf&& v) { append(forward<Tf>(v)); return *this; }
    template<perfect(T)> void appendOnce(Tf&& v) { if(!contains(v)) append(forward<Tf>(v)); }

    /// append array
    void append(array&& a) { int s=size()+a.size(); reserve(s); copy((byte*)end(),(byte*)a.data(),a.size()*sizeof(T)); setSize(s); }
    void append(const array& a) { reserve(size()+a.size()); for(const auto& e: a) append(copy(e)); }
    array& operator <<(array&& a) { append(move(a)); return *this; }
    array& operator <<(const array& a) { append(a); return *this; }

    void fill(const T& value, int size) { resize(size); for(auto& e: *this) e=copy(value); }

    /// convenience methods for data serialization
    void skip(int skip) { grow(size()+skip); }
    template <int width> void align() { grow(::align<width>(size())); }

    /// insert
    template<perfect(T)> void insertAt(int index, Tf&& v) {
        reserve(size()+1); setSize(size()+1);
        for(int i=size()-2;i>=index;i--) copy(at(i+1),at(i));
        new (addressof(at(index))) T(forward<Tf>(v));
    }
    template<perfect(T)> void insertSorted(Tf&& v) {
        int i=0;
        for(;i<size();i++) if(v < at(i)) break;
        insertAt(i,forward<Tf>(v));
    }
    void insertSorted(array&& a) {
        int s=size()+a.size(); reserve(s);
        for(auto&& e: a) insertSorted(move(e));
    }

    /// iterators
    const T* begin() const { return data(); }
    const T* end() const { return data()+size(); }
    T* begin() { return (T*)data(); }
    T* end() { return (T*)data()+size(); }
};

/// comparison
template<class T> bool operator ==(const array<T>& a, const array<T>& b) {
    if(a.size() != b.size()) return false;
    for(int i=0;i<a.size();i++) if(!(a[i]==b[i])) return false;
    return true;
}
template<class T> bool operator !=(const array<T>& a, const array<T>& b) { return !(a==b); }

/// Copies all elements in a new array
template<class T> array<T> copy(const array<T>& a) { array<T> r; r<<a; return  r; }

/// Slices an array referencing elements from \a pos to \a pos + \a size
/// \note Using move semantics, this operation is safe without refcounting the data buffer
template<class T> array<T> slice(array<T>&& a, int pos,int size) {
    assert_(pos>=0 && pos+size<=a.size());
    assert_(pos == 0 || a.capacity() == 0); //only allow slicing of referencing arrays. TODO: custom realloc with slicing
    return array<T>(a.data()+pos,size);
}
/// Slices an array referencing elements from \a pos to the end of the array
template<class T> array<T> slice(array<T>&& a, int pos) { return slice(move(a),pos,a.size()-pos); }

/// Slices an array copying elements from \a pos to \a pos + \a size
template<class T> array<T> slice(const array<T>& a, int pos,int size) {
    assert_(pos>=0 && pos+size<=a.size());
    return copy(array<T>(a.data()+pos,size));
}
/// Slices an array copying elements from \a pos to the end of the array
template<class T> array<T> slice(const array<T>& a, int pos) { return slice(a,pos,a.size()-pos); }

/// Reverses elements in-place
template<class T> array<T> reverse(array<T>&& a) { for(int i=0; i<a.size()/2; i++) swap(a[i], a[a.size()-i-1]); return move(a); }
/// Copy with reversed elements
//template<class T> array<T> reverse(const array<T>& a) { array<T> r(a.size); r.size=a.size; for(int i=0;i<a.size;i++) new((T*)&r+a.size-1-i) T(copy(a[i])); return r; }

/// Replaces in \a array every occurence of \a before with \a after
template<class T> array<T>  replace(array<T>&& array, const T& before, const T& after) { for(auto& e : array) if(e==before) e=copy(after); return move(array); }
/*/// Copy \a array with every occurence of \a before replaced with \a after
template<class T> array<T>  replace(const array<T>& a, const T& before, const T& after) {
    array<T> r(a.size); r.size=a.size; for(int i=0;i<a.size;i++) new ((T*)&r+i) T(copy(a[i]==before? after : a[i])); return r;
}*/

template<class T> const T& min(const array<T>& a) { T* min=&a.first(); for(T& e: a) if(e<*min) min=&e; return *min; }
template<class T> T& max(array<T>& a) { T* max=&a.first(); for(T& e: a) if(e>*max) max=&e; return *max; }
template<class T> T sum(const array<T>& a) { T sum=0; for(const T& e: a) sum+=e; return sum; }
