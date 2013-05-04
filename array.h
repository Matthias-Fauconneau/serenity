#pragma once
/// \file array.h Contiguous collection of elements
#include "core.h"

/// Contiguous collection of elements (can hold either a const reference or a mutable heap allocation).
/// \note like buffer<T> with resize/insert/remove using T constructor/destructor
template<Type T> struct array {
    /// Default constructs an empty inline array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(uint capacity, uint size=0):size(size){assert(capacity>=size); reserve(capacity);}
    /// Allocates a buffer for \a capacity elements and fill with value
    array(uint capacity, uint size, const T& value):size(size){assert(capacity>=size); reserve(capacity); ::clear(data,size,value);}
    /// Copies elements from a reference
    explicit array(const ref<T>& ref){reserve(ref.size); size=ref.size; for(uint i: range(ref.size)) new (data+i) T(ref[i]);}
    /// References \a size elements from const \a data pointer
    array(const T* data, uint size):data((T*)data),size(size){}
    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(capacity) { for(uint i: range(size)) data[i].~T(); free(data); } data=0; }

    array(array&& o) { data=o.data, capacity=o.capacity, size=o.size; o.data=0, o.capacity=o.size=0; }
    array& operator=(array&& o){ this->~array(); new (this) array(move(o)); return *this; }

    /// Returns true if not empty
    explicit operator bool() const { return size; }
    /// Returns a const reference to the elements contained in this array
    operator ref<T>() const { return ref<T>(data,size); }
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// \name Iterators
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    T* begin() { return data; }
    T* end() { return data+size; }
    /// \}

    /// Slices a const reference to elements from \a pos to \a pos + \a size
    ref<T> slice(uint pos, uint size) const { assert(pos+size<=this->size); return ref<T>(data+pos,size); }
    /// Slices a const reference to elements from \a pos the end of the array
    ref<T> slice(uint pos) const { assert(pos<=size); return ref<T>(data+pos,size-pos); }
    /// Slices a mutable reference to elements from \a pos to \a pos + \a size
    memory<T> iterate(uint pos, uint size) { assert(pos+size<=this->size); return {data+pos, data+pos+size}; }
    /// Slices a mutable reference to elements from \a pos the end of the array
    memory<T> iterate(uint pos) { assert(pos<=size); return {data+pos, data+size}; }

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

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity) {
        if(capacity>this->capacity) {
            assert(capacity>=size);
            if(this->capacity) data=(T*)realloc(data, capacity*sizeof(T)); //reallocate heap buffer (copy is done by allocator if necessary)
            else data=(T*)malloc(capacity*sizeof(T));
            this->capacity=capacity;
        }
    }
    /// Resizes the array to \a size and default initialize new elements
    void grow(uint size) { uint old=this->size; assert(size>old); reserve(size); this->size=size; for(uint i: range(old,size)) new (data+i) T(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size) { assert(capacity && size<=this->size); for(uint i: range(size,this->size)) at(i).~T(); this->size=size; }
    /// Removes all elements
    void clear() { if(size) shrink(0); }

    /// Appends a new element directly on the array (without move)
    template<Type... Args> void append(Args&&... args) { uint s=size+1; reserve(s); new (end()) T(forward<Args>(args)...); size=s;}

    /// \name Append operators
    array& operator<<(T&& e) { uint s=size+1; reserve(s); new (end()) T(move(e)); size=s; return *this; }
    array& operator<<(array<T>&& a) {uint s=size+a.size; reserve(s); copy((byte*)end(),(byte*)a.data,a.size*sizeof(T)); size=s; return *this; }
    array& operator<<(const T& v) { *this<< copy(v); return *this; }
    array& operator<<(const ref<T>& a) {uint s=size; reserve(size=s+a.size); for(uint i: range(a.size)) new (data+s+i) T(copy(a[i])); return *this; }
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
        for(int i=size-2;i>=index;i--) copy((byte*)(data+i+1),(const byte*)(data+i),sizeof(T));
        new (data+index) T(move(e));
        return at(index);
    }
    /// Inserts a value at \a index
    T& insertAt(int index, const T& v) { return insertAt(index,copy(v)); }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(T&& e) { uint i=0; while(i<size && at(i) < e) i++; insertAt(i,move(e)); return i; }
    /// Inserts immediately before the first element greater than or equal to the argument
    int insertSorted(const T& v) { return insertSorted(copy(v)); }

    /// Removes one element at \a index
    void removeAt(uint index) { at(index).~T(); for(uint i: range(index, size-1)) copy((byte*)(data+i),(byte*)(data+i+1),sizeof(T)); size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size-1); }

    /// Removes one matching element and returns an index to its successor
    template<Type K> int removeOne(const K& key) { int i=indexOf(key); if(i>=0) removeAt(i); return i; }
    /// Removes all matching elements
    template<Type K> void removeAll(const K& key) { for(uint i=0; i<size;) if(at(i)==key) removeAt(i); else i++; }
    /// Filters elements matching predicate
    template<Type F> void filter(F f) { for(uint i=0; i<size;) if(f(at(i))) removeAt(i); else i++; }

    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
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

    T* data=0; /// Pointer to the buffer valid while not reallocated
    uint capacity=0; /// 0: const reference, >0: size of the owned heap allocation
    uint size=0; /// Number of elements currently in this array
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

/// string is an array of bytes
typedef array<byte> string;
