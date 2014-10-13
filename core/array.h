#pragma once
/// \file array.h Contiguous collection of elements
#include "memory.h"

generic struct remove_const { typedef T type; };
generic struct remove_const<T const> { typedef T type; };
generic struct decay { typedef typename remove_const<typename remove_reference<T>::type>::type type; };

struct true_type { static constexpr bool value = true; };
struct false_type{ static constexpr bool value = false; };
template<Type A, Type B> struct is_same : false_type {};
generic struct is_same<T, T> : true_type {};

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

template<bool B, Type T = void> struct enable_if {};
generic struct enable_if<true, T> { typedef T type; };

/// Managed initialized dynamic mutable reference to an array of elements (either an heap allocation managed by this object or a reference to memory managed by another object)
/// \note Adds resize/insert/remove using T constructor/destructor
generic struct array : buffer<T> {
    using buffer<T>::data;
    using buffer<T>::size;
    using buffer<T>::capacity;

    default_move(array);
    /// Default constructs an empty array
    array() {}
    /// Converts a buffer to an array
    array(buffer<T>&& o) : buffer<T>(move(o)) {}
    /// Allocates an empty array with storage space for \a capacity elements
    explicit array(size_t capacity) : buffer<T>(capacity, 0) {}
    /// Moves elements from a reference
    explicit array(const mref<T> ref) : buffer<T>(ref.size) { mref<T>::move(ref); }
    /// Copies elements from a reference
    explicit array(const ref<T> ref) : buffer<T>(ref.size) { mref<T>::copy(ref); }

    /// If the array owns the reference, destroys all initialized elements
    ~array() { if(capacity) { for(size_t i: range(size)) at(i).~T(); } }

    using buffer<T>::end;
    using buffer<T>::at;
    using buffer<T>::last;
    using buffer<T>::slice;

    /// Allocates enough memory for \a capacity elements
    void reserve(size_t nextCapacity) {
        if(nextCapacity>capacity) {
            assert(nextCapacity>=size);
            if(capacity) {
                data=(T*)realloc((T*)data, nextCapacity*sizeof(T)); // Reallocates heap buffer (copy is done by allocator if necessary)
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

    /// Appends a default element
    T& append() {
        size_t s=size+1; reserve(s); new (end()) T; size=s; return last();
    }
    /// Appends an implicitly copiable value
    T& append(const T& e) {
        size_t nextSize=size+1; reserve(nextSize); new (end()) T(             e); size=nextSize; return last();
    }
    /// Appends a movable value
    T& append(T&& e) {
        size_t nextSize=size+1; reserve(nextSize); new (end()) T(::move(e)); size=nextSize; return last();
    }
    /// Appends another list of elements to this array by moving
    void append(const mref<T> source) {
        if(source) { size_t oldSize=size; reserve(size=oldSize+source.size); slice(oldSize,source.size).move(source); }
    }
    /// Appends another list of elements to this array by copying
    void append(const ref<T> source) {
        if(source) { size_t oldSize=size; reserve(size=oldSize+source.size); slice(oldSize,source.size).copy(source); }
    }
    /// Appends a new element
    template<Type Arg, typename enable_if<!is_convertible<Arg, T>::value && !is_convertible<Arg, ref<T>>::value>::type* = nullptr>
    T& append(Arg&& arg) {
        size_t s=size+1; reserve(s); new (end()) T{forward<Arg>(arg)}; size=s; return last();
    }
    /// Appends a new element
    template<Type Arg0, Type Arg1, Type... Args> T& append(Arg0&& arg0, Arg1&& arg1, Args&&... args) {
        size_t s=size+1; reserve(s); new (end()) T{forward<Arg0>(arg0), forward<Arg1>(arg1), forward<Args>(args)...}; size=s; return last();
    }

    /// Appends the element, if it is not present already
    template<Type F> T& add(F&& e) { size_t i = indexOf(e); if(i!=invalid) return at(i); else return append(forward<F>(e)); }

    /// Inserts an element at \a index
    template<Type V> T& insertAt(int index, V&& e) {
        assert(index>=0);
        reserve(++size);
        for(int i=size-2;i>=index;i--) raw(at(i+1)).copy(raw(at(i)));
        new (&at(index)) T(move(e));
        return at(index);
    }
    /// Inserts immediately before the first element greater than the argument
    template<Type V> int insertSorted(V&& e) { size_t i=0; while(i<size && at(i) <= e) i++; insertAt(i, ::move(e)); return i; }

    /// Removes one element at \a index
    void removeAt(size_t index) { at(index).~T(); for(size_t i: range(index, size-1)) raw(at(i)).copy(raw(at(i+1))); size--; }
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

/// Concatenates two buffers by appending (when first operand can be moved, is otherwise unused)
generic inline buffer<T> operator+(buffer<T>&& a, const ref<T> b) {
    array<T> target(move(a)); target.append(b); return move(target);
}

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

// -- Miscellaneous --

/// Creates a new array containing only elements where filter \a predicate does not match
template<Type T, Type Function> array<T> filter(const ref<T> source, Function predicate) {
    array<T> target(source.size); for(const T& e: source) if(!predicate(e)) target.append(copy(e)); return target;
}

// -- String --

/// array<char> holding UTF8 text strings
struct String : array<char> {
    using array::array;
    String() {}
    explicit String(string source) : array<char>(source) {}
    template<size_t N> explicit constexpr String(const char (&a)[N]) : array<char>(string(a, N-1)) {}
    using array::append;
    void append(const string source) { return array::append(source); }
    template<size_t N> void append(const char (&source)[N]) { return array::append(string(source, N-1)); }
};
template<> inline String copy(const String& o) { return copy((const array<char>&)o); }
// Resolves ambiguities
inline String operator+(const char a, const string b) { return operator+<char>(a, b); }
inline String operator+(const char a, const String& b) { return operator+<char>(a, b); }
inline String operator+(const string a, char b) { return operator+<char>(a, b); }
inline String operator+(const string a, const string b) { return operator+<char>(a, b); }
inline String operator+(const string a, const String& b) { return operator+<char>(a, b); }
inline String operator+(const String& a, const char b) { return operator+<char>(a, b); }
inline String operator+(const String& a, const string b) { return operator+<char>(a, b); }
inline String operator+(const String& a, const String& b) { return operator+<char>(a, b); }
inline String operator+(String&& a, const char b) { return operator+<char>(move(a), b); }
inline String operator+(String&& a, const string b) { return operator+<char>(move(a), b); }
inline String operator+(String&& a, const String& b) { return operator+<char>(move(a), b); }

/// Converts Strings to strings
inline buffer<string> toRefs(const ref<String>& source) { return apply(source, [](const String& e) -> string { return  e; }); }
