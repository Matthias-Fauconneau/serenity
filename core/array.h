#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"

/// Managed initialized dynamic mutable reference to an array of elements (either an heap allocation managed by this object or a reference to memory managed by another object)
/// \note Adds resize/insert/remove using T constructor/destructor
generic struct array : buffer<T> {
    default_move(array);
    /// Default constructs an empty array
    array() {}
    /// References \a o.size elements from \a o.data pointer
    explicit array(const ref<T>& o): buffer<T>(o) {}
    /// Moves elements from a reference
    explicit array(const mref<T>& ref) : buffer<T>(ref.size) { move(*this, ref); }
    /// Converts a buffer to an array (move constructor)
    array(buffer<T>&& o) : buffer<T>(move(o)) {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(size_t capacity) : buffer<T>(capacity, 0) {}
    /// If the array owns the reference, destroys all initialized elements
    ~array() { if(capacity) { for(size_t i: range(size)) at(i).~T(); } }

    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Allocates enough memory for \a capacity elements
    void reserve(size_t nextCapacity) {
        if(nextCapacity>capacity) {
            assert(nextCapacity>=size);
            if(capacity) {
#if 1 //FIXME: invalidates references
                data=(T*)realloc((T*)data, nextCapacity*sizeof(T)); //reallocate heap buffer (raw copy is done by allocator if necessary)
#else // FIXME: should use realloc when possible
                T* oldPointer = (T*)data;
                if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error(__FILE__, nextCapacity);
                move(mref<T>((T*)data, size), mref<T>(oldPointer, size));
                free(oldPointer);
#endif
                assert(size_t(data)%alignof(T)==0);
            } else if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error(__FILE__, nextCapacity);
            capacity=nextCapacity;
        }
    }
    /// Resizes the array to \a size and default initialize new elements
    void grow(size_t nextSize) { size_t previousSize=size; assert(nextSize>=previousSize); reserve(nextSize); size=nextSize; slice(previousSize,nextSize-previousSize).clear(); }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(size_t nextSize) { assert(capacity && nextSize<=size); for(size_t i: range(nextSize,size)) data[i].~T(); size=nextSize; }
    /// Removes all elements
    void clear() { if(size) shrink(0); }

    /// Appends a new element constructing it directly into the array (avoids using a move operations)
    template<Type... Args> void append(Args&&... args) { reserve(size+1); buffer<T>::append(forward<Args>(args)...); }
    array& operator<<(const T& e) { append(e); return *this; }
    array& operator<<(T&& e) { append(move(e)); return *this; }

    /// Appends values
    void append(const ref<T>& a) { reserve(size+a.size); buffer<T>::append(a); }
    array& operator<<(const ref<T>& a) { append(a); return *this; }

    /// Appends elements
    void append(buffer<T>&& a) { reserve(size+a.size); buffer<T>::append(move(a)); }
    array& operator<<(buffer<T>&& a) { append(move(a)); return *this; }

    /// \name Appends once (if not already contained) operators
    array& operator+=(const T& e) { if(!contains(e)) append(e); return *this; }
    array& operator+=(T&& e) { if(!contains(e)) append(move(e)); return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this += v; return *this; }

    /// Inserts an element at \a index
    template<Type V> T& insertAt(int index, V&& e) {
        assert(index>=0);
        reserve(++size);
        for(int i=size-2;i>=index;i--) copy(raw(at(i+1)), raw(at(i)));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts immediately before the first element greater than or equal to the argument
    template<Type V> int insertSorted(V&& e) { size_t i=0; while(i<size && at(i) < e) i++; insertAt(i,move(e)); return i; }

    /// Removes one element at \a index
    void removeAt(size_t index) { at(index).~T(); for(size_t i: range(index, size-1)) copy(raw(at(i)), raw(at(i+1))); size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size-1); }

    /// Removes one matching element and returns an index to its successor
    template<Type K> int tryRemove(const K& key) { int i=indexOf(key); if(i>=0) removeAt(i); return i; }
    /// Removes one matching element and returns an index to its successor, aborts if not found
    template<Type K> int remove(const K& key) { int i=indexOf(key); assert(i>=0); removeAt(i); return i; }
    /// Filters elements matching predicate
    template<Type F> array& filter(F f) { for(size_t i=0; i<size;) if(f(at(i))) removeAt(i); else i++; return *this; }

    /// Returns the index of the first occurence of \a key. Returns -1 if \a key could not be found.
    template<Type K> int indexOf(const K& key) const { for(size_t i: range(size)) { if(data[i]==key) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    template<Type K> bool contains(const K& key) const { return indexOf(key)>=0; }
    /// Returns index to the first element greater than or equal to \a value using linear search (assuming a sorted array)
    template<Type K> int linearSearch(const K& key) const { size_t i=0; while(i<size && at(i) < key) i++; return i; }
    /// Returns index to the first element greater than or equal to \a value using binary search (assuming a sorted array)
    template<Type K> int binarySearch(const K& key) const {
        size_t min=0, max=size;
        while(min<max) {
            size_t mid = (min+max)/2;
            assert(mid<max);
            if(at(mid) < key) min = mid+1;
            else max = mid;
        }
        assert(min == max);
        return min;
    }

    using buffer<T>::data;
    using buffer<T>::size;
    using buffer<T>::capacity;
    using buffer<T>::slice;
    using buffer<T>::at;
};

/// Copies all elements in a new array
generic array<T> copy(const array<T>& o) { return copy((const buffer<T>&)o); }

/// Replaces in \a array every occurence of \a before with \a after
generic array<T> replace(array<T>&& a, const T& before, const T& after) {
    for(T& e : a) if(e==before) e=after; return move(a);
}

/// Copies elements not matching predicate in a new array
template<Type T, Type F> array<T> filter(const ref<T>& a, F f) { array<T> r (a.size); for(const T& e: a) if(!f(e)) r << copy(e); return r; }

/// Copies indices not matching predicate in a new array
template<Type T, Type F> array<T> filterIndex(const ref<T>& a, F f) { array<T> r (a.size); for(const size_t i: range(a.size)) if(!f(i)) r << copy(a[i]); return r; }

/// Returns an array of the application of a function to every index up to a size
template<Type Function, Type... Args> auto apply(range a, Function function, Args... args) -> buffer<decltype(function(0, args...))> {
    buffer<decltype(function(0, args...))> r(a.stop-a.start); for(uint i: range(a.stop-a.start)) new (&r[i]) decltype(function(0, args...))(function(a.start+i, args...)); return r;
}

/// Returns an array of the application of a function to every elements of a reference
template<Type T, Type Function, Type... Args> auto apply(const ref<T>& a, Function function, Args... args) -> buffer<decltype(function(a[0], args...))> {
    buffer<decltype(function(a[0], args...))> r(a.size);
    for(uint i: range(a.size)) new (&r[i]) decltype(function(a[0], args...))(function(a[i], args...));
    return r;
}

/// Returns an array of the application of a constructor to every elements of a reference
template<Type C, Type T, Type... Args> auto apply(const ref<T>& a, Args&&... args) -> buffer<C> {
    buffer<C> r(a.size);
    for(uint i: range(a.size)) new (&r[i]) C(a[i], args...);
    return r;
}

/// Converts arrays to references
generic buffer<ref<T> > toRefs(const ref<array<T>>& o) { return apply(o, [](const array<T>& o)->ref<T>{return o;}); }

/// String is an array of bytes
typedef array<byte> String;

generic uint partition(const mref<T>& at, size_t left, size_t right, size_t pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}

generic T quickselect(const mref<T>& at, size_t left, size_t right, size_t k) {
    for(;;) {
        size_t pivotIndex = partition(at, left, right, (left + right)/2);
        size_t pivotDist = pivotIndex - left + 1;
        if(pivotDist == k) return at[pivotIndex];
        else if(k < pivotDist) right = pivotIndex - 1;
        else { k -= pivotDist; left = pivotIndex + 1; }
    }
}
/// Quickselects the median in-place
generic T median(const mref<T>& at) { if(at.size==1) return at[0u]; return quickselect(at, 0, at.size-1, at.size/2); }

generic void quicksort(const mref<T>& at, int left, int right) {
    if(left < right) { // If the list has 2 or more items
        int pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic void quicksort(const mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); }
