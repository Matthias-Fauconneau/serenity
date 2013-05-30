#pragma once
/// \file array.h Contiguous collection of elements
#include "core.h"

/// Contiguous collection of elements stored in a memory buffer
/// \note Adds resize/insert/remove using T constructor/destructor
template<Type T> struct array : buffer<T> {
    default_move(array);
    /// Default constructs an empty array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(uint capacity, uint64 size=0) : buffer<T>(capacity, size) {}
    /// Allocates a buffer for \a capacity elements and fill with value
    array(uint capacity, uint64 size, const T& value)  : buffer<T>(capacity, size, value) {}
    /// Copies elements from a reference
    explicit array(const ref<T>& ref) : buffer<T>(ref.size) { for(uint i: range(ref.size)) new (this->data+i) T(ref[i]); }
    /// References \a size elements from const \a data pointer
    array(const T* data, uint size) : buffer<T>((T*)data, size) {}
    /// If the array owns the reference, destroys all initialized elements
    ~array() { if(this->capacity) { for(uint i: range(this->size)) this->data[i].~T(); } }

    explicit operator const T*() const { return this->data; }
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity) {
        if(capacity>this->capacity) {
            assert(capacity>=this->size);
            if(this->capacity) this->data=(T*)realloc(this->data, capacity*sizeof(T)); //reallocate heap buffer (copy is done by allocator if necessary)
            else this->data=(T*)malloc(capacity*sizeof(T));
            this->capacity=capacity;
        }
    }
    /// Resizes the array to \a size and default initialize new elements
    void grow(uint size) { uint old=this->size; assert(size>old); reserve(size); this->size=size; for(uint i: range(old,size)) new (this->data+i) T(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) { assert(this->capacity && size<=this->size); for(uint i: range(size,this->size)) this->data[i].~T(); this->size=size; }
    /// Removes all elements
    void clear() { if(this->size) shrink(0); }

    /// Appends a new element constructing it directly into the array (avoids using a move operations)
    template<Type... Args> void append(Args&&... args) { uint s=this->size+1; reserve(s); new (this->end()) T(forward<Args>(args)...); this->size=s;}

    /// \name Append operators
    array& operator<<(T&& e) { uint s=this->size+1; reserve(s); new (this->end()) T(move(e)); this->size=s; return *this; }
    array& operator<<(array<T>&& a) {uint s=this->size; reserve(this->size=s+a.size); for(uint i: range(a.size)) new (this->data+s+i) T(move(a[i])); return *this; }
    array& operator<<(const T& v) { *this<< copy(v); return *this; }
    array& operator<<(const ref<T>& a) {uint s=this->size; reserve(this->size=s+a.size); for(uint i: range(a.size)) new (this->data+s+i) T(copy(a[i])); return *this; }
    /// \}

    /// \name Appends once (if not already contained) operators
    array& operator +=(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& operator +=(array&& b) { for(T& v: b) *this+= move(v); return *this; }
    array& operator +=(const T& v) { if(!contains(v)) *this<<copy(v); return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this+=v; return *this; }
    /// \}

    /// Inserts an element at \a index
    T& insertAt(int index, T&& e) {
        assert(index>=0);
        reserve(++this->size);
        for(int i=this->size-2;i>=index;i--) copy((byte*)(this->data+i+1),(const byte*)(this->data+i),sizeof(T));
        new (this->data+index) T(move(e));
        return this->at(index);
    }
    /// Inserts a value at \a index
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(T&& e) { uint i=0; while(i<this->size && this->at(i) < e) i++; insertAt(i,move(e)); return i; }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes one element at \a index
    void removeAt(uint index) { this->at(index).~T(); for(uint i: range(index, this->size-1)) copy((byte*)(this->data+i),(byte*)(this->data+i+1),sizeof(T)); this->size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(this->at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(this->size-1); }

    /// Removes one matching element and returns an index to its successor
    template<Type K> int tryRemove(const K& key) { int i=indexOf(key); if(i>=0) removeAt(i); return i; }
    /// Removes one matching element and returns an index to its successor, aborts if not contained
    template<Type K> int remove(const K& key) { int i=indexOf(key); assert(i>=0); removeAt(i); return i; }
    /// Removes all matching elements
    template<Type K> void removeAll(const K& key) { for(uint i=0; i<this->size;) if(this->at(i)==key) removeAt(i); else i++; }
    /// Filters elements matching predicate
    template<Type F> void filter(F f) { for(uint i=0; i<this->size;) if(f(this->at(i))) removeAt(i); else i++; }

    /// Returns the index of the first occurence of \a key. Returns -1 if \a key could not be found.
    template<Type K> int indexOf(const K& key) const { for(uint i: range(this->size)) { if(this->data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    template<Type K> bool contains(const K& key) const { return indexOf(key)>=0; }
    /// Returns index to the first element greater than or equal to \a value using linear search (assuming a sorted array)
    template<Type K> int linearSearch(const K& key) const { uint i=0; while(i<this->size && this->at(i) < key) i++; return i; }
    /// Returns index to the first element greater than or equal to \a value using binary search (assuming a sorted array)
    template<Type K> int binarySearch(const K& key) const {
        uint min=0, max=this->size;
        while(min<max) {
            uint mid = (min+max)/2;
            assert(mid<max);
            if(this->at(mid) < key) min = mid+1;
            else max = mid;
        }
        assert(min == max /*&& at(min) == key*/);
        return min;
    }
    /// Returns a pointer to the first occurrence of \a key. Returns 0 if \a key could not be found.
    template<Type K> T* find(const K& key) { int i = indexOf(key); return i>=0 ? &this->at(i) : 0; }
};

/// Copies all elements in a new array
template<Type T> array<T> copy(const array<T>& o) { array<T> copy; copy<<o; return copy; }

/// Concatenates two arrays
template<Type T> inline array<T> operator+(const ref<T>& a, const ref<T>& b) { array<T> r; r<<a; r<<b; return r; }
template<Type T> inline array<T> operator+(const array<T>& a, const ref<T>& b) { array<T> r; r<<a; r<<b; return r; }
template<Type T> inline array<T> operator+(const ref<T>& a, const array<T>& b) { array<T> r; r<<a; r<<b; return r; }
template<Type T> inline array<T> operator+(const array<T>& a, const array<T>& b) { array<T> r; r<<a; r<<b; return r; }

/// Replaces in \a array every occurence of \a before with \a after
template<Type T> array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=copy(after); return move(a);
}

/// Returns an array of the application of a function to every elements of a reference
template<class O, class T, class F> array<O> apply(const ref<T>& a, F function) {
        array<O> r; for(const T& e: a) r << function(e); return r;
}
template<class O, class T, class F> array<O> apply(const array<T>& a, F function) {
        array<O> r; for(const T& e: a) r << function(e); return r;
}

/// string is an array of bytes
typedef array<byte> string;
