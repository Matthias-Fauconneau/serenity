#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"

/// Managed initialized dynamic mutable reference to an array of elements (either an heap allocation managed by this object or a reference to memory managed by another object)
/// \note Adds resize/insert/remove using T constructor/destructor
template<Type T> struct array : buffer<T> {
    default_move(array);
    /// Default constructs an empty array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(uint capacity/*, size_t size=0*/) : buffer<T>(capacity, /*size*/0) {}
    /// Copies elements from a reference
    explicit array(const ref<T>& ref) : buffer<T>(ref.size) { for(uint i: range(ref.size)) new (&at(i)) T(ref[i]); }
    /// Converts a buffer to an array
    array(buffer<T>&& o) : buffer<T>(move(o)) {}
    /// If the array owns the reference, destroys all initialized elements
    ~array() { if(capacity) { for(uint i: range(size)) at(i).~T(); } }

    explicit operator const T*() const { return data; } // Explicits ref::operator const T*
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity) {
        if(capacity>this->capacity) {
            assert(capacity>=size);
            if(this->capacity) data=(T*)realloc((T*)data, capacity*sizeof(T)); //reallocate heap buffer (copy is done by allocator if necessary)
            else data=(T*)malloc(capacity*sizeof(T));
            this->capacity=capacity;
        }
    }
    /// Resizes the array to \a size and default initialize new elements
    void grow(uint size) { uint old=this->size; assert(size>=old); reserve(size); this->size=size; for(uint i: range(old,size)) new (&at(i)) T(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) { assert(capacity && size<=this->size); for(uint i: range(size,this->size)) data[i].~T(); this->size=size; }
    /// Removes all elements
    void clear() { if(size) shrink(0); }

    /// Appends a new element constructing it directly into the array (avoids using a move operations)
    template<Type... Args> void append(Args&&... args) { uint s=size+1; reserve(s); new (end()) T(forward<Args>(args)...); size=s;}

    /// \name Append operators
    array& operator<<(T&& e) { uint s=size+1; reserve(s); new (end()) T(move(e)); size=s; return *this; }
    array& operator<<(array<T>&& a) {uint s=size; reserve(size=s+a.size); for(uint i: range(a.size)) new (&at(s+i)) T(move(a[i])); return *this; }
    array& operator<<(const T& v) { *this<< copy(v); return *this; }
    array& operator<<(const ref<T>& a) {uint s=size; reserve(size=s+a.size); for(uint i: range(a.size)) new (&at(s+i)) T(copy(a[i])); return *this; }
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
        reserve(++size);
        for(int i=size-2;i>=index;i--) copy((byte*)&at(i+1),(const byte*)&at(i),sizeof(T));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts a value at \a index
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(T&& e) { uint i=0; while(i<size && at(i) < e) i++; insertAt(i,move(e)); return i; }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes one element at \a index
    void removeAt(uint index) { at(index).~T(); for(uint i: range(index, size-1)) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size-1); }

    /// Removes one matching element and returns an index to its successor
    template<Type K> int tryRemove(const K& key) { int i=indexOf(key); if(i>=0) removeAt(i); return i; }
    /// Removes one matching element and returns an index to its successor, aborts if not contained
    template<Type K> int remove(const K& key) { int i=indexOf(key); assert(i>=0); removeAt(i); return i; }
    /// Removes all matching elements
    template<Type K> void removeAll(const K& key) { for(uint i=0; i<size;) if(at(i)==key) removeAt(i); else i++; }
    /// Filters elements matching predicate
    template<Type F> void filter(F f) { for(uint i=0; i<size;) if(f(at(i))) removeAt(i); else i++; }

    /// Returns the index of the first occurence of \a key. Returns -1 if \a key could not be found.
    template<Type K> int indexOf(const K& key) const { for(uint i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    template<Type K> bool contains(const K& key) const { return indexOf(key)>=0; }
    /// Returns index to the first element greater than or equal to \a value using linear search (assuming a sorted array)
    template<Type K> int linearSearch(const K& key) const { uint i=0; while(i<size && at(i) < key) i++; return i; }
    /// Returns index to the first element greater than or equal to \a value using binary search (assuming a sorted array)
    template<Type K> int binarySearch(const K& key) const {
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

    using buffer<T>::data;
    using buffer<T>::size;
    using buffer<T>::capacity;
    using buffer<T>::end;
    using buffer<T>::at;
};
/// Copies all elements in a new array
template<Type T> array<T> copy(const array<T>& o) { return copy((const buffer<T>&)o); }

/// Concatenates two arrays
template<Type T> inline array<T> operator+(const ref<T>& a, const ref<T>& b) { array<T> r; r<<a; r<<b; return r; }

/// Replaces in \a array every occurence of \a before with \a after
template<Type T> array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=copy(after); return move(a);
}

/// Returns an array of the application of a function to every elements of a reference
template<class O, class T, class F, class... Args> array<O> apply(const ref<T>& a, F function, Args... args) {
    array<O> r; for(const T& e: a) r << function(e, args...); return r;
}

/// Converts arrays to references
template<Type T> array<ref<T>> toRefs(const ref<array<T>>& o) {
    array<ref<T>> r; for(const array<T>& e: o) r << (const ref<T>&)e; return r;
}

/// String is an array of bytes
typedef array<byte> String;
