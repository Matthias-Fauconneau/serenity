#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"

// -- traits

struct true_type { static constexpr bool value = true; };
struct false_type{ static constexpr bool value = false; };
template<Type A, Type B> struct is_same : false_type {};
generic struct is_same<T, T> : true_type {};
template<bool B, Type T = void> struct enable_if {};
generic struct enable_if<true, T> { typedef T type; };
generic struct declval_protector {
    static const bool stop = false;
    static T&& delegate();
};
generic inline T&& declval() noexcept {
    static_assert(declval_protector<T>::__stop, "declval() must not be used!");
    return declval_protector<T>::__delegate();
}
template<Type From, Type To> struct is_convertible {
    template<Type T> static void test(T);
    template<Type F, Type T, Type = decltype(test<T>(declval<F>()))> static true_type test(int);
    template<Type, Type> static false_type test(...);
    static constexpr bool value = decltype(test<From, To>(0))::value;
};

/// Managed initialized dynamic mutable reference to an array of elements
/// Data is either embedded/inline or an heap allocation managed by this object or a reference to memory managed by another object
/// \note Adds resize/insert/remove using T constructor/destructor
generic struct array : buffer<T> {
    using buffer<T>::data;
    using buffer<T>::size;
    using buffer<T>::capacity;
    byte inlineBuffer[0]; // Address of any inline buffer
    bool isInline() const { return data==(T*)inlineBuffer; }

    /// Default constructs an empty array
    array() {}
    /// Converts a buffer to an array
    array(buffer<T>&& o) : buffer<T>(move(o)) {}
    /// Allocates an empty array with storage space for \a capacity elements
    explicit array(size_t nextCapacity) : array() { reserve(nextCapacity); }
    /// Copies elements from a reference
    explicit array(const ref<T> source) : array() { append(source); }
    /// Moves elements from a reference
    explicit array(const mref<T> source) : array() { append(source); } //FIXME: only enables for non implicitly copiable types

    array(array&& o) : buffer<T>((T*)o.data, o.size, o.capacity) {
        if(o.isInline()) {
            capacity = 0;
            reserve(o.size);
            mref<T>::move(o);
        }
        o.data=0, o.size=0, o.capacity=0;
    }
    array& operator=(array&& o) { this->~array(); new (this) array(::move(o)); return *this; }

    /// If the array owns the reference, destroys all initialized elements
    ~array() {
        if(capacity) {
            for(size_t i: range(size)) at(i).~T();
            if(isInline()) capacity=0; /*Prevents buffer from free'ing*/
        }
    }

    using buffer<T>::end;
    using buffer<T>::at;
    using buffer<T>::last;
    using buffer<T>::slice;
    using buffer<T>::set;

    /// Allocates enough memory for \a capacity elements
    size_t reserve(size_t nextCapacity) {
        assert(nextCapacity>=size);
        if(nextCapacity>capacity) {
			bool wasHeap = !isInline() && capacity;
            const T* data = 0;
            // TODO: move compatible realloc
            if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error("Out of memory", size, capacity, nextCapacity, sizeof(T));
            swap(data, this->data);
            capacity = nextCapacity;
            mref<T>::move(mref<T>((T*)data, size));
			if(wasHeap) free((void*)data);
        }
        return nextCapacity;
    }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(size_t nextSize) { assert(capacity && nextSize<=size); for(size_t i: range(nextSize,size)) data[i].~T(); size=nextSize; }
    /// Removes all elements
    void clear() { if(size) shrink(0); }

    /// Appends a default element
    T& append() { size=reserve(size+1); return set(size-1, T()); }
    /// Appends an implicitly copiable value
    T& append(const T& e) { size=reserve(size+1); return set(size-1, T(e)); }
    /// Appends a movable value
    T& append(T&& e) { size=reserve(size+1); return set(size-1, T(::move(e))); }
    /// Appends another list of elements to this array by moving
    void append(const mref<T> source) { size=reserve(size+source.size); slice(size-source.size).move(source); }
    /// Appends another list of elements to this array by copying
    void append(const ref<T> source) { size=reserve(size+source.size); slice(size-source.size).copy(source); }
    /// Appends a new element
    template<Type Arg, typename enable_if<!is_convertible<Arg, T>::value && !is_convertible<Arg, ref<T>>::value>::type* = nullptr>
    T& append(Arg&& arg) { size=reserve(size+1); return set(size-1, T{forward<Arg>(arg)}); }
    /// Appends a new element
    template<Type Arg0, Type Arg1, Type... Args> T& append(Arg0&& arg0, Arg1&& arg1, Args&&... args) {
        size=reserve(size+1); return set(size-1, T{forward<Arg0>(arg0), forward<Arg1>(arg1), forward<Args>(args)...});
    }

    /// Appends the element, if it is not present already
    template<Type F> T& add(F&& e) { size_t i = indexOf(e); if(i!=invalid) return at(i); else return append(forward<F>(e)); }

    /// Inserts an element at \a index
    template<Type V> T& insertAt(int index, V&& e) {
        assert(index>=0);
        size=reserve(size+1);
        if(int(size)-2>=index) {
            set(size-1, move(at(size-2))); // Initializes last slot
            for(int i=size-3;i>=index;i--) at(i+1)= move(at(i)); // Shifts elements
            return at(index)=move(e);
        } else return set(index, move(e)); // index==size-1
    }
    /// Inserts immediately before the first element greater than the argument
    template<Type V> int insertSorted(V&& e) { size_t i=0; while(i<size && at(i) <= e) i++; insertAt(i, ::move(e)); return i; }

    /// Removes one element at \a index
    void removeAt(size_t index) { at(index).~T(); for(size_t i: range(index, size-1)) at(i)=move(at(i+1)); size--; }
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
generic array<T> copy(const array<T>& o) { return array<T>((const ref<T>)o); }

/// Allocates an inline buffer on stack to store small arrays without an heap allocation
template<Type T, size_t N=128> struct Array : array<T> {
    using array<T>::data;
    using array<T>::size;
    using array<T>::capacity;
    byte inlineBuffer[N-sizeof(array<T>)]; // Pads array<T> reference to cache line size to hold small arrays without managing an heap allocation
    static constexpr size_t inlineBufferCapacity = sizeof(inlineBuffer)/sizeof(T);

    /// Default constructs an empty array
    Array() : array<T>(buffer<T>((T*)inlineBuffer, 0, inlineBufferCapacity)) {}
    /// Converts a buffer to an array
    Array(buffer<T>&& o) : array<T>(move(o)) { assert_(capacity > inlineBufferCapacity, capacity); }
    /// Allocates an empty array with storage space for \a capacity elements
    explicit Array(size_t nextCapacity) : Array() { reserve(nextCapacity); }
    /// Copies elements from a reference
    explicit Array(const ref<T> source) : Array() { append(source); }
    /// Moves elements from a reference
    explicit Array(const mref<T> source) : Array() { append(source); } //FIXME: only enables for non implicitly copiable types

    Array(Array&& o) : array<T>(buffer<T>((T*)o.data, o.size, o.capacity)) {
        if(o.isInline()) {
            data = (T*)inlineBuffer;
            capacity = inlineBufferCapacity;
            reserve(o.size);
            mref<T>::move(o);
        }
        o.data=0, o.size=0, o.capacity=0;
    }
    Array& operator=(Array&& o) { this->~Array(); new (this) Array(::move(o)); return *this; }

    using array<T>::reserve;
    using array<T>::append;
};
/// Copies all elements in a new array
generic Array<T> copy(const Array<T>& o) { return Array<T>((const ref<T>)o); }

// -- Sort --

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
generic T median(const mref<T>& at) { if(at.size==1) return at[0]; return quickselect(at, 0, at.size-1, at.size/2); }

generic void quicksort(const mref<T>& at, int left, int right) {
    if(left < right) { // If the list has 2 or more items
        int pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic const mref<T>& sort(const mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); return at; }

// -- Functional  --

/// Creates a new array containing only elements where filter \a predicate does not match
template<Type T, Type Function> array<T> filter(const ref<T> source, Function predicate) {
    array<T> target(source.size); for(const T& e: source) if(!predicate(e)) target.append(copy(e)); return target;
}
