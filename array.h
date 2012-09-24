#pragma once
/// \file array.h Common array container
#include "core.h"
#include "memory.h"

/// Polyvalent memory reference (const or mutable, owned or reference, inline or on heap).
/// \note Uses move semantics to avoid reference counting when managing an heap buffer
/// \note Stores small arrays inline (<=31bytes)
template<class T> struct array {
    int8 tag = 0; //0: empty, >0: inline, -1 = reference, -2 = heap buffer
    struct{const T* data; uint size; uint capacity;} buffer = {0,0,0};
    int pad[2]; //Pads to fill half a cache line (tag:1+7+data:8+size:4+capacity:4+2*4 = 31 bytes inline capacity)
    /// Number of elements fitting inline
    static constexpr uint inline_capacity() { return (sizeof(array)-1)/sizeof(T); }
    /// Pointer to the buffer valid while the array is not reallocated (resize or inline array move)
    T* data() { assert(tag!=-1); return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
    const T* data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
    /// Number of elements currently in this array
    uint size() const { assert((tag==-1) || ((tag>=0?tag:buffer.size)<=capacity())); return tag>=0?tag:buffer.size; }
    /// Sets size without any construction/destruction. Use resize to default initialize new values.
    void setSize(uint size) { assert(size<=capacity()); if(tag>=0) tag=size; else buffer.size=size;}
    /// Maximum number of elements without reallocation (0 for references)
    uint capacity() const { return tag>=0 ? inline_capacity() : buffer.capacity; }

    // Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    // \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array);
    move_operator(array) { if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy((byte*)this,(byte*)&o,sizeof(array)); o.tag=0; }

    /// Default constructs an empty inline array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(uint capacity){reserve(capacity); }
    /// Copies elements from a reference
    explicit array(const ref<T>& ref){reserve(ref.size); setSize(ref.size); for(uint i: range(ref.size)) new (&at(i)) T(ref[i]);}
    /// References \a size elements from read-only \a data pointer
    array(const T* data, uint size) : tag(-1) { buffer=__(data, size, 0); }

    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(tag!=-1) { for(uint i: range(size())) data()[i].~T(); if(tag==-2) unallocate(buffer.data,buffer.capacity); } }

    /// Allocates strictly enough memory for \a capacity elements
    void setCapacity(uint capacity) {
        assert(tag!=-1);
        assert(capacity>=size());
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

    /// Resizes the array to \a size and default initialize new elements
    void grow(uint size) { uint old=this->size(); assert(size>old); reserve(size); setSize(size); for(uint i: range(old,size)) new (&at(i)) T(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) {
        assert(size<=this->size());
        if(tag!=-1) for(uint i: range(size,this->size())) at(i).~T();
        setSize(size);
    }
    /// Sets the array size to 0, destroying any contained elements
    void clear() { if(size()) shrink(0); }

    /// Slices a reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { return ref<T>(*this).slice(pos,size); }
    /// Slices a reference to elements from \a pos the end of the array
    ref<T> slice(uint pos) const { return ref<T>(*this).slice(pos); }

    /// Returns true if not empty
    explicit operator bool() const { return size(); }
    /// Returns a reference to the elements contained in this array
    operator ref<T>() const { return ref<T>(data(),size()); }
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// \name Accessors
    const T& at(uint i) const { assert(i<size()); return data()[i]; }
    T& at(uint i) { assert(i<size()); return (T&)data()[i]; }
    const T& operator [](uint i) const { return at(i); }
    T& operator [](uint i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size()-1); }
    T& last() { return at(size()-1); }
    /// \}

    /// Returns index of the first element matching \a value
    int indexOf(const T& value) const { return ref<T>(*this).indexOf(value); }
    /// Returns whether this array contains any elements matching \a value
    bool contains(const T& value) const { return ref<T>(*this).contains(value); }

    /// \name Append operators
    array& operator <<(T&& e) { int s=size()+1; reserve(s); new (end()) T(move(e)); setSize(s); return *this; }
    array& operator <<(array<T>&& a) { int s=size()+a.size(); reserve(s); copy((byte*)end(),(byte*)a.data(),a.size()*sizeof(T)); setSize(s); return *this; }
    array& operator <<(const T& v) { *this<< copy(v); return *this; }
    array& operator <<(const ref<T>& a) { int old=size(); reserve(old+a.size); setSize(old+a.size); for(uint i: range(a.size)) new (&at(old+i)) T(copy(a[i])); return *this; }
    /// \}

    /// \name Appends once (if not already contained) operators
    array& operator +=(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& operator +=(array&& b) { for(T& v: b) *this+= move(v); return *this; }
    array& operator +=(const T& v) { if(!contains(v)) *this<< copy(v); return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this+= copy(v); return *this; }
    /// \}

    /// Inserts an element at \a index
    T& insertAt(int index, T&& e) {
        reserve(size()+1); setSize(size()+1);
        for(int i=size()-2;i>=index;i--) copy((byte*)&at(i+1),(const byte*)&at(i),sizeof(T));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts a value at \a index
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    /// Inserts an element immediatly after the first lesser value in array
    int insertSorted(T&& e) { uint i=0; for(;i<size() && at(i) < e;i++) {} insertAt(i,move(e)); return i; }
    /// Inserts a value immediatly after the first lesser value in array
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes one element at \a index
    void removeAt(uint index) { at(index).~T(); for(uint i: range(index, size()-1)) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); setSize(size()-1); }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size()-1); }
    /// Removes one matching element and returns an index to its successor
    int removeOne(const T& v) { int i=indexOf(v); if(i>=0) removeAt(i); return i; }
    /// Removes all matching elements
    void removeAll(const T& v) { for(uint i=0; i<size();) if(at(i)==v) removeAt(i); else i++; }

    /// \name Iterators
    const T* begin() const { return data(); }
    const T* end() const { return data()+size(); }
    T* begin() { return (T*)data(); }
    T* end() { return (T*)data()+size(); }
    /// \}
};

/// Copies all elements in a new array
template<class T> array<T> copy(const array<T>& o) { array<T> copy; copy<<o; return copy; }

/// Replaces in \a array every occurence of \a before with \a after
template<class T> array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=copy(after); return move(a);
}

/// string is an array of bytes
typedef array<byte> string;
