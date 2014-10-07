#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"

/// Managed initialized dynamic mutable reference to an array of elements (either an heap allocation managed by this object or a reference to memory managed by another object)
/// \note Adds resize/insert/remove using T constructor/destructor
generic struct array : buffer<T> {
    using buffer<T>::data;
    using buffer<T>::size;
    using buffer<T>::capacity;

    default_move(array);
    /// Default constructs an empty array
    array() {}
    /// Allocates an uninitialized buffer for \a capacity elements
    explicit array(size_t capacity) : buffer<T>(capacity, 0) {}
    /// Moves elements from a reference
    explicit array(const mref<T>& ref) : buffer<T>(ref.size) { move(*this, ref); }
    /// Copies elements from a reference
    explicit array(const ref<T>& ref) : buffer<T>(ref.size) { copy(*this, ref); }
    /// Converts a buffer to an array
    array(buffer<T>&& o) : buffer<T>(move(o)) {}
    /// If the array owns the reference, destroys all initialized elements
    ~array() { if(capacity) { for(size_t i: range(size)) at(i).~T(); } }

    using buffer<T>::end;
    using buffer<T>::at;
    using buffer<T>::slice;

    /// Compares all elements
    //bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Allocates enough memory for \a capacity elements
    void reserve(size_t nextCapacity) {
        if(nextCapacity>capacity) {
            assert(nextCapacity>=size);
            if(capacity) {
                data=(T*)realloc((T*)data, nextCapacity*sizeof(T)); //reallocate heap buffer (copy is done by allocator if necessary)
                assert(size_t(data)%alignof(T)==0);
            } else if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error("");
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
    template<Type Arg, Type... Args> array& append(Arg&& arg, Args&&... args) { size_t s=size+1; reserve(s); new (end()) T(forward<Arg>(arg), forward<Args>(args)...); size=s; return *this;}
    T& append() { size_t s=size+1; reserve(s); new (end()) T(); size=s; return mref<T>::last(); }

    /// \name Append operators
    array& operator<<(T&& e) { size_t s=size+1; reserve(s); new (end()) T(move(e)); size=s; return *this; }
    array& operator<<(array<T>&& a) {size_t s=size; reserve(size=s+a.size); move(slice(s,a.size), a); return *this; }
    array& operator<<(const T& v) { size_t s=size+1; reserve(s); new (end()) T(v); size=s; return *this; }
    array& operator<<(const ref<T>& a) {size_t s=size; reserve(size=s+a.size); copy(slice(s,a.size), a); return *this; }
    /// \}

    /// \name Appends once (if not already contained) operators
    array& operator +=(T&& v) { if(!contains(v)) *this<< move(v); return *this; }
    array& operator +=(const T& v) { if(!contains(v)) *this<<v; return *this; }
    array& operator +=(const ref<T>& o) { for(const T& v: o) *this+=v; return *this; }
    /// \}

    /// Inserts an element at \a index
    template<Type V> T& insertAt(int index, V&& e) {
        assert(index>=0);
        reserve(++size);
        for(int i=size-2;i>=index;i--) copy(raw(at(i+1)), raw(at(i)));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts immediately before the first element greater than the argument
    template<Type V> int insertSorted(V&& e) { size_t i=0; while(i<size && at(i) <= e) i++; insertAt(i,move(e)); return i; }

    /// Removes one element at \a index
    void removeAt(size_t index) { at(index).~T(); for(size_t i: range(index, size-1)) copy(raw(at(i)), raw(at(i+1))); size--; }
    /// Removes one element at \a index and returns its value
    T take(int index) { T value = move(at(index)); removeAt(index); return value; }
    /// Removes the last element and returns its value
    T pop() { return take(size-1); }

    /// Removes one matching element and returns an index to its successor
    template<Type K> int tryRemove(const K& key) { int i=indexOf(key); if(i>=0) removeAt(i); return i; }
    /// Removes one matching element and returns an index to its successor, aborts if not sed
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
};
/// Copies all elements in a new array
generic array<T> copy(const array<T>& o) { return copy((const buffer<T>&)o); }

/// Concatenates two arrays
generic inline array<T> operator+(const ref<T>& a, const ref<T>& b) { array<T> r; r<<a; r<<b; return r; }

/// Filters elements matching predicate
template<Type T, Type F> array<T> filter(const ref<T>& a, F f) { array<T> r(a.size); for(const T& e: a) if(!f(e)) r << copy(e); return r; }

/// Stores the application of a function to every index up to a size in a mref
template<Type T, Type Function> void apply(mref<T> target, Function function) {
    for(size_t index: range(target.size)) new (&target[index]) T(function(index));
}

/// Stores the application of a function to every elements of a ref in a mref
template<Type T, Type Function, Type S0, Type... Ss> void apply(mref<T> target, Function function, ref<S0> source0, ref<Ss>... sources) {
    for(size_t index: range(target.size)) new (&target[index]) T(function(source0[index], sources[index]...));
}

/// Returns an array of the application of a function to every index up to a size
template<Type Function, Type... Args> auto apply(Function function, size_t size, Args... args) -> buffer<decltype(function(0, args...))> {
    buffer<decltype(function(0, args...))> target(size); apply(target, function); return target;
}

/// Returns an array of the application of a function to every elements of a reference
template<Type Function, Type S0, Type... Ss>
auto apply(Function function, ref<S0> source0, ref<Ss>... sources) -> buffer<decltype(function(source0[0], sources[0]...))> {
    buffer<decltype(function(source0[0], sources[0]...))> target(source0.size);
    /*for(size_t index: range(target.size))
        new (&target[index]) decltype(function(source0[0], sources[0]...))(function(source0[index], sources[index]...));*/
    apply(target, function, source0, sources...);
    return target;
}

/// Replaces in \a array every occurence of \a before with \a after
template<Type T, Type B, Type A> void replace(array<T>& a, const B& before, const A& after) {
    for(T& e : a) if(e==before) e=T(after);
}

/// Converts arrays to references
generic array<ref<T> > toRefs(const ref<array<T>>& o) {
    array<ref<T>> r; for(const array<T>& e: o) r << (const ref<T>&)e; return r;
}

/// String is an array of bytes
typedef array<char> String;

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
generic const mref<T>& sort(const mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); return at; }
