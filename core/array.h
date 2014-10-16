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
    byte inlineBuffer[is_same<T,char>::value?40:0]; // Pads array<T> reference to cache line size to hold small arrays without managing an heap allocation
    static constexpr size_t inlineBufferCapacity = sizeof(inlineBuffer); ///sizeof(T);
    bool isInline() const { return data==(T*)inlineBuffer; }

    /// Default constructs an empty array
    array() : buffer<T>((T*)inlineBuffer, 0, inlineBufferCapacity) {}
    /// Converts a buffer to an array
    array(buffer<T>&& o) : buffer<T>(move(o)) { assert_(capacity > inlineBufferCapacity, capacity); }
    /// Allocates an empty array with storage space for \a capacity elements
    explicit array(size_t nextCapacity) : array() { reserve(nextCapacity); }
    /// Copies elements from a reference
    explicit array(const ref<T> source) : array() { append(source); }
    /// Moves elements from a reference
    explicit array(const mref<T> source) : array() { append(source); } //FIXME: only enables for non implicitly copiable types

    array(array&& o) : buffer<T>((T*)o.data, o.size, o.capacity) {
        if(o.isInline()) {
            data=(T*)inlineBuffer;
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
            const T* data = 0;
            if(posix_memalign((void**)&data,16,nextCapacity*sizeof(T))) error("Out of memory"); // TODO: move compatible realloc
            swap(data, this->data);
            capacity = nextCapacity;
            mref<T>::move(mref<T>((T*)data, size));
        }
        return nextCapacity;
    }
    /// Resizes the array to \a size and default initialize new elements
    //void grow(size_t nextSize) { size_t previousSize=size; size=reserve(nextSize); slice(previousSize).clear(); }
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
generic array<T> copy(const array<T>& o) {
    static_assert(sizeof(array<T>)<=64,""); // Just here as this function should be the first to be instantiated on a new T for array<T>
    return array<T>((const ref<T>)o);
}

// -- Concatenate --

/// Concatenates a \a cat with a cat
template<class A, class B, class T> struct cat {
    A a; B b;
    cat(A&& a, B&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size(); }
    void copy(array<T>& target) const { a.copy(target); b.copy(target); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B, class C, class D>
cat<cat<A, B, T>, cat<C, D, T>, T> operator+(cat<A, B, T>&& a, cat<C, D, T>&& b) { return {move(a),move(b)}; }

/// Concatenates a \a cat with a value
template<class A, class T> struct cat<A, T, T> {
    A a; const T b;
    cat(A&& a, const T b) : a(move(a)), b(b) {}
    int size() const { return a.size() + 1; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B> cat<cat<A, B, T>, T, T> operator+(cat<A, B, T>&& a, T b) { return {move(a),b}; }

/// Concatenates a \a cat with a ref
template<class A, class T> struct cat<A, ref<T>, T> {
    A a; const ref<T> b;
    cat(A&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size() + b.size; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, class A, class B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates a \a cat with an array
template<class A, class T> struct cat<A, array<T>, T> {
    A a; array<T> b;
    //cat(const cat)=delete; // Deletes copy constructor to avoid dangling references
    cat(A&& a, array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size() + b.size; };
    void copy(array<T>& target) const { a.copy(target); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B>
cat<cat<A, B, T>, array<T>, T> operator+(cat<A, B, T>&& a, array<T>&& b) { return {move(a),move(b)}; }

/// Concatenate a value with a ref
generic struct cat<T, ref<T>, T> {
    T a; ref<T> b;
    cat(T a, ref<T> b) : a(a), b(b) {}
    int size() const { return 1 + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<T, ref<T>, T> operator+(T a, ref<T> b) { return {a,b}; }

/// Concatenates a ref with a value
generic struct cat<ref<T>, T, T> {
    ref<T> a; T b;
    cat(ref<T> a, T b) : a(a), b(b) {}
    int size() const { return a.size + 1; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<ref<T>, T, T> operator+(ref<T> a, T b) { return {a,b}; }

/// Concatenates a ref with a ref
generic struct cat<ref<T>, ref<T>, T> {
    ref<T> a; ref<T> b;
    cat(ref<T> a, ref<T> b) : a(a), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<ref<T>,ref<T>, T> operator+(ref<T> a, ref<T> b) { return {a,b}; }
// Required for implicit string literal conversion
//inline cat<ref<char>,ref<char>, T> operator+(ref<char> a, ref<char> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(const T(&a)[N], ref<T> b) { return {a,b}; }
template<class T, size_t N> cat<ref<T>,ref<T>, T> operator+(ref<T> a, const T(&b)[N]) { return {a,b}; }

/// Concatenates a ref with an array
generic struct cat<ref<T>, array<T>, T> {
    ref<T> a; array<T> b;
    cat(ref<T> a, array<T>&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<ref<T>,array<T>, T> operator+(ref<T> a, array<T>&& b) { return {a,move(b)}; }
// Required for implicit string literal conversion
//inline cat<ref<char>,array<char>, char> operator+(ref<char> a, array<char>&& b) { return {a,move(b)}; }
//template<class T, class A, class B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a) { return {move(a),b}; }
template<class T, size_t N> cat<ref<T>,array<T>, T> operator+(const T(&a)[N], array<T>&& b) { return {a,move(b)}; }

/// Concatenates a ref with a cat
template<class B, class T> struct cat<ref<T>, B, T> {
    const ref<T> a; B b;
    cat(ref<T> a, B&& b) : a(a), b(move(b)) {}
    int size() const { return a.size + b.size(); };
    void copy(array<T>& target) const { target.append(a); b.copy(target); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
template<class T, class A, class B> cat<ref<T>, cat<A, B, T>, T> operator+(ref<T> a, cat<A, B, T>&& b) { return {a,move(b)}; }
// Required for implicit string literal conversion
//template<class T, class A, class B, size_t N> cat<cat<A, B, T>, ref<T>, T> operator+(cat<A, B, T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates an array with a value
generic struct cat<array<T>, T, T> {
    array<T> a; T b;
    cat(array<T>&& a, T b) : a(move(a)), b(b) {}
    int size() const { return a.size + 1; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<array<T>, T, T> operator+(array<T>&& a, T b) { return {move(a),b}; }

/// Concatenates an array with a ref
generic struct cat<array<T>, ref<T>, T> {
    array<T> a; ref<T> b;
    cat(array<T>&& a, ref<T> b) : a(move(a)), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<array<T>,ref<T>, T> operator+(array<T>&& a, ref<T> b) { return {move(a),b}; }
// Required for implicit string literal conversion
template<class T, size_t N> cat<array<T>,ref<T>, T> operator+(array<T>&& a, const T(&b)[N]) { return {move(a),b}; }

/// Concatenates an array with an array
generic struct cat<array<T>, array<T>, T> {
    array<T> a; array<T> b;
    cat(array<T>&& a, array<T>&& b) : a(move(a)), b(move(b)) {}
    int size() const { return a.size + b.size; };
    void copy(array<T>& target) const { target.append(a); target.append(b); }
    operator array<T>() const { array<T> target (size()); copy(target); return move(target); }
    //operator ref<T>() const { return array<T>(); }
};
generic cat<array<T>,array<T>, T> operator+(array<T>&& a, array<T>&& b) { return {move(a),move(b)}; }

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
