#pragma once
#include "core.h"
#include "memory.h"

/// \a array is a typed bounded mutable growable memory region
/// \note array uses move semantics to avoid reference counting when holding an heap buffer
/// \note array stores small arrays inline (<=31bytes)
/// \note array copies to an heap buffer when modifying a reference
template<class T> struct array {
    int8 tag = 0; //0: empty, >0: inline, -1 = reference, -2 = heap buffer, -3 = static buffer
    struct Buffer {
        const T* data;
        uint size;
        uint capacity;
        Buffer(const T* data, uint size, uint capacity):data(data),size(size),capacity(capacity){}
    } buffer = {0,0,0};
    int pad[2]; //1+7+8+4+4+2*4=32 Pads to fill half a cache line (31 inline bytes)
    /// Number of elements fitting inline
    static constexpr uint inline_capacity() { return (sizeof(array)-1)/sizeof(T); }
    /// Data pointer valid while the array is not reallocated (resize or inline array move)
    T* data() { assert_(tag!=-1); return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
    const T* data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
    /// Number of elements currently in this array
    uint size() const { assert_((tag==-1) || ((tag>=0?tag:buffer.size)<=capacity())); return tag>=0?tag:buffer.size; }
    /// Sets size without any construction/destruction. /sa resize
    void setSize(uint size) { assert_(size<=capacity()); if(tag>=0) tag=size; else buffer.size=size;}
    /// Maximum number of elements without reallocation (0 for references)
    uint capacity() const { return tag>=0 ? inline_capacity() : buffer.capacity; }

    /// Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    /// \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array)

    /// Default constructs an empty inline array
    array() {}

    /// Move constructor
    array(array&& o) { if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy((byte*)this,(byte*)&o,sizeof(array)); o.tag=0; }
    /// Move assignment
    array& operator=(array&& o) { this->~array(); if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy((byte*)this,(byte*)&o,sizeof(array)); o.tag=0; return *this; }
    /// Allocates a new uninitialized array for \a capacity elements
    explicit array(uint capacity){reserve(capacity); }
    /// Moves elements from a reference
    explicit array(ref<T>&& ref){reserve(ref.size); setSize(ref.size); for(uint i=0;i<ref.size;i++) new (&at(i)) T(move((T&)ref[i]));}
    /// Copies elements from a reference
    explicit array(const ref<T>& ref){reserve(ref.size); setSize(ref.size); for(uint i=0;i<ref.size;i++) new (&at(i)) T(copy(ref[i]));}
    /// References \a size elements from read-only \a data pointer
    array(const T* data, uint size) : tag(-1), buffer(data, size, 0) {} //TODO: escape analysis
    /// Initializes array with a writable buffer of \a capacity elements
    array(const T* data, uint size, uint capacity) : tag(-3), buffer(data, size, capacity) { assert_(capacity>inline_capacity()); }

    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(tag!=-1) { for(uint i=0,size=this->size();i<size;i++) data()[i].~T(); if(tag==-2) unallocate(buffer.data,buffer.capacity); } }

    /// Allocates strictly enough memory for \a capacity elements
    void setCapacity(uint capacity) {
        assert_(tag!=-1);
        assert_(capacity>=size());
        if(tag==-2 && buffer.capacity) { //already on heap: reallocate
            reallocate<T>((T*&)buffer.data,buffer.capacity,capacity);
            buffer.capacity=capacity;
        } else if(capacity <= inline_capacity()) {
            if(tag>=0) return; //already inline
            tag=buffer.size;
            copy((byte*)(&tag+1),(byte*)buffer.data,buffer.size*sizeof(T));
        } else { //inline or reference: allocate
            T* heap=allocate<T>(capacity);
            copy((byte*)heap,(byte*)data(),size()*sizeof(T));
            buffer.data=heap;
            if(tag>=0) buffer.size=tag;
            tag=-2;
            buffer.capacity=capacity;
        }
    }
    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity) { if(capacity > this->capacity()) setCapacity(capacity); }

    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) {
        assert_(size<=this->size());
        if(tag!=-1) for(uint i=size;i<this->size();i++) at(i).~T();
        setSize(size);
    }
    /// Sets the array size to 0, destroying any contained elements
    void clear() { if(size()) shrink(0); }
    void grow(uint size) { uint old=this->size(); assert_(size>old); reserve(size); setSize(size); for(uint i=old;i<size;i++) new (&at(i)) T(); }
    void resize(uint size) { if(size<this->size()) shrink(size); else if(size>this->size()) grow(size); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { return ref<T>(*this).slice(pos,size); }
    /// Slices a reference to elements from to the end of the array
    ref<T> slice(uint pos) const { return ref<T>(*this).slice(pos); }

    /// Returns true if not empty
    explicit operator bool() const { return size(); }
    /// Returns a reference to the elements contained in this array
    operator ref<T>() const { return ref<T>(data(),size()); } //TODO: escape analysis
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }
    /// Compares to single value
    bool operator ==(const T& value) const { return  (ref<T>)*this==value; }

    /// Accessors
    /// \note Use \a ref to avoid inline checking or \a data() to avoid bound checking in performance critical code
    const T& at(uint i) const { assert_(i<size()); return data()[i]; }
    T& at(uint i) { assert_(i<size()); return (T&)data()[i]; }
    const T& operator [](uint i) const { return at(i); }
    T& operator [](uint i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size()-1); }
    T& last() { return at(size()-1); }

    /// Finds elements
    int indexOf(const T& value) const { return ref<T>(*this).indexOf(value); }
    bool contains(const T& value) const { return ref<T>(*this).contains(value); }

    /// Appends elements
    array& operator <<(T&& e) { int s=size()+1; reserve(s); new (end()) T(move(e)); setSize(s); return *this; }
    array& operator <<(array<T>&& a) { int s=size()+a.size(); reserve(s); copy((byte*)end(),(byte*)a.data(),a.size()*sizeof(T)); setSize(s);
                                       return *this; }
    array& operator <<(const T& v) { *this<< copy(v); return *this; }
    array& operator <<(const ref<T>& a) {
        int old=size(); reserve(old+a.size); setSize(old+a.size); for(uint i=0;i<a.size;i++) new (&at(old+i)) T(copy(a[i]));
        return *this;
    }
    /// Appends once (if not already contained)
    array& operator +=(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& operator +=(array&& b) { for(T& v: b) *this+= move(v); return *this; }
    array& operator +=(const T& v) { if(!contains(v)) *this<< copy(v); return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this+= copy(v); return *this; }

    /// Inserts elements
    T& insertAt(int index, T&& e) {
        reserve(size()+1); setSize(size()+1);
        for(int i=size()-2;i>=index;i--) copy((byte*)&at(i+1),(const byte*)&at(i),sizeof(T));
        new (&at(index)) T(move(e));
        return at(index);
    }
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    int insertSorted(T&& e) { uint i=0; for(;i<size() && at(i) < e;i++) {} insertAt(i,move(e)); return i; }
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes elements
    void removeAt(uint i) { at(i).~T(); for(;i<size()-1;i++) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); setSize(size()-1); }
    T take(int i) { T value = move(at(i)); removeAt(i); return value; }
    T pop() { return take(size()-1); }
    /// Removes one matching element and returns an index to its successor
    int removeOne(const T& v) { int i=indexOf(v); if(i>=0) removeAt(i); return i; }
    /// Removes all matching elements
    bool removeAll(const T& v) { bool any=false; for(uint i=0;i<size();i++) if(at(i)==v) { removeAt(i); i--; any=true; } return any; }

    /// Iterators
    const T* begin() const { return data(); }
    const T* end() const { return data()+size(); }
    T* begin() { return (T*)data(); }
    T* end() { return (T*)data()+size(); }
};

/// Copies all elements in a new array
template<class T> array<T> copy(const array<T>& a) { return array<T>((ref<T>)a); }

/// Replaces in \a array every occurence of \a before with \a after
template<class T> array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=copy(after); return move(a);
}

/// string is an array of bytes
typedef array<byte> string;
