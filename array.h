#pragma once
/// \file array.h Contiguous collection of elements
#include "core.h"

/// Contiguous collection of elements (can hold either a const reference or a mutable heap allocation).
/// \note Uses move semantics to avoid reference counting when managing an heap allocation
template<Type T> struct array {
    T* data=0; /// Pointer to the buffer valid while not reallocated
    uint size=0; /// Number of elements currently in this array
    uint capacity=0; /// 0: const reference, >0: size of the owned heap allocation

    move_operator(array) { data=o.data, size=o.size, capacity=o.capacity; o.data=0,o.size=o.capacity=0; }

    /// Default constructs an empty inline array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(uint capacity, uint size=0):size(size){assert(capacity>=size); reserve(capacity);}
    /// Allocates a buffer for \a capacity elements and fill with value
    array(uint capacity, uint size, const T& value):size(size){assert(capacity>=size); reserve(capacity); ::clear(data,size,value);}
    /// Copies elements from a reference
    explicit array(const ref<T>& ref){reserve(ref.size); size=ref.size; for(uint i: range(ref.size)) new (&at(i)) T(ref[i]);}
    /// References \a size elements from const \a data pointer
    array(const T* data, uint size):data((T*)data),size(size){}
    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(capacity) { for(uint i: range(size)) data[i].~T(); unallocate(data); } }
    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity) {
        if(capacity>this->capacity) {
            assert(capacity>=size);
            reallocate<T>(data, this->capacity=capacity); //reallocate heap buffer (copy is done by allocator if necessary)
        }
    }
    /// Resizes the array to \a size and default initialize new elements
    void grow(uint size) { uint old=this->size; assert(size>old); reserve(size); this->size=size; for(uint i: range(old,size)) new (&at(i)) T(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) { assert(capacity && size<=this->size); for(uint i: range(size,this->size)) at(i).~T(); this->size=size; }
    /// Removes all elements
    void clear() { if(size) shrink(0); }

    /// Returns true if not empty
    explicit operator bool() const { return size; }
    /// Returns a const reference to the elements contained in this array
    operator ref<T>() const { return ref<T>(data,size); }
    /// Returns a mutable reference to the elements contained in this array
    explicit operator mutable_ref<T>() { return mutable_ref<T>(data,size); }
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Slices a const reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { return ref<T>(*this).slice(pos,size); }
    /// Slices a const reference to elements from \a pos the end of the array
    ref<T> slice(uint pos) const { return ref<T>(*this).slice(pos); }
    /// Slices a mutable reference to elements from \a pos to \a pos + \a size
    mutable_ref<T> mutable_slice(uint pos, uint size) { return mutable_ref<T>(*this).slice(pos,size); }
    /// Slices a mutable reference to elements from \a pos the end of the array
    mutable_ref<T> mutable_slice(uint pos) { return mutable_ref<T>(*this).slice(pos); }

    /// \name Accessors
    const T& at(uint i) const { assert(i<size,i,size); return data[i]; }
    T& at(uint i) { assert(i<size,i,size); return data[i]; }
    const T& operator [](uint i) const { return at(i); }
    T& operator [](uint i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size-1); }
    T& last() { return at(size-1); }
    /// \}

    /// \name Append operators
    array& operator<<(T&& e) {uint s=size+1; reserve(s); new (end()) T(move(e)); size=s; return *this; }
    array& operator<<(array<T>&& a) {uint s=size+a.size; reserve(s); copy((byte*)end(),(byte*)a.data,a.size*sizeof(T)); size=s; return *this; }
    array& operator<<(const T& v) { *this<< copy(v); return *this; }
    array& operator<<(const ref<T>& a) {uint s=size; reserve(size=s+a.size); for(uint i: range(a.size)) new (&at(s+i)) T(copy(a[i])); return *this; }
    /// \}

    /// \name Appends once (if not already contained) operators
    array& operator +=(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& operator +=(array&& b) { for(T& v: b) *this+= move(v); return *this; }
    array& operator +=(const T& v) { if(!contains(v)) *this<<copy(v); return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this+=v; return *this; }
    /// \}

    /// \name Appends once (if not already contained) operators
    array& appendOnce(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& appendOnce(array&& b) { for(T& v: b) appendOnce(move(v)); return *this; }
    array& appendOnce(const T& v) { if(!contains(v)) *this<<copy(v); return *this; }
    array& appendOnce(const ref<T>& o) { for(const T& v: o) appendOnce(v); return *this; }
    /// \}

    /// Inserts an element at \a index
    T& insertAt(int index, T&& e) {
        reserve(++size);
        for(int i=size-2;i>=index;i--) copy((byte*)&at(i+1),(const byte*)&at(i),sizeof(T));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts a value at \a index
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    /// Inserts an element immediatly after the first lesser value in array
    flatten int insertSorted(T&& e) { uint i=0; while(i<size && at(i) < e) i++; insertAt(i,move(e)); return i; } //O_o: +30ms on poisson (never used)
    /// Inserts a value immediatly after the first lesser value in array
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes one element at \a index
    void removeAt(uint index) { at(index).~T(); for(uint i: range(index, size-1)) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size-1); }
    /// Removes one matching element and returns an index to its successor
    int removeOne(const T& v) { int i=indexOf(v); if(i>=0) removeAt(i); return i; }
    /// Removes all matching elements
    void removeAll(const T& v) { for(uint i=0; i<size;) if(at(i)==v) removeAt(i); else i++; }

    /// \name Iterators
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    T* begin() { return data; }
    T* end() { return data+size; }
    /// \}

    /// Returns index of the first element matching \a value
    int indexOf(const T& key) const { return ref<T>(*this).indexOf(key); }
    /// Returns whether this array contains any elements matching \a value
    bool contains(const T& key) const { return ref<T>(*this).contains(key); }
    /// Returns index to the last element less than or equal to \a value using binary search (assuming a sorted array)
    flatten int binarySearch(const T& key) const {
        uint min=0, max=size;
        while(min<max) {
            uint mid = (min+max)/2;
            assert(mid<max);
            if(at(mid) < key) min = mid+1;
            else max = mid;
        }
        assert(min == max /*&& at(min) == key*/);
        return min;
    }

    /// Copies elements to \a target and increments pointer
    void cat(T*& target) const { ::copy(target,data,size); target+=size; }
};

/// Copies all elements in a new array
template<Type T> array<T> copy(const array<T>& o) { array<T> copy; copy<<o; return copy; }

/// Replaces in \a array every occurence of \a before with \a after
template<Type T> array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=copy(after); return move(a);
}

/// string is an array of bytes
typedef array<byte> string;
