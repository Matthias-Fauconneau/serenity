#pragma once
#include "core.h"

#define generic template<class T>

namespace std {
template<class T> struct initializer_list {
    no_copy(initializer_list)
    const T* data;
    uint size;
    /// References \a size elements from read-only \a data pointer
    constexpr initializer_list(const T* data, uint size) : data(data), size(size) {}
    /// References elements sliced from \a begin to \a end
    constexpr initializer_list(const T* begin,const T* end) : data(begin), size(uint(end-begin)) {}
    constexpr initializer_list(initializer_list&& o):data(o.data),size(o.size){}
    initializer_list& operator =(initializer_list&& o){data=o.data;size=o.size; return *this;}
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert_(i<size); return data[i]; }
    explicit operator bool() const { return size; }
};
}
/// \a ref is a const typed bounded memory reference (i.e fat pointer)
/// \note As \a data is not owned, ref should never be stored, only used as argument.
i( template<class T> using ref = std::initializer_list<T>; )

/// Slices a reference to elements from \a pos to \a pos + \a size
generic ref<T> slice(const ref<T>& a, uint pos, uint size) { assert_(pos+size<=a.size); return ref<T>(a.data+pos,size); }
/// Slices a reference to elements from to the end of the reference
generic ref<T> slice(const ref<T>& a, uint pos) { assert_(pos<=a.size); return ref<T>(a.data+pos,a.size-pos); }

//Comparable?
/// Compares all elements
generic bool operator ==(const ref<T>& a, const ref<T>& b);
/// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
generic int indexOf(const ref<T>& a, const T& value);
/// Returns true if the array contains an occurrence of \a value
generic bool contains(const ref<T>& a, const T& value);

/// \a array is a typed bounded mutable growable memory reference (heap/stack/static/mmap)
/// \note array use move semantics to avoid reference counting when managing heap reference
/// \note array transparently store small arrays inline (<=15bytes)
/// \note array transparently detach when trying to modify a foreign (i.e not owned heap) reference
/// \note This header only defines basic container methods, #include "array.cc" to define advanced methods
template<class T> struct array {
    int8 tag = -1; //0: empty, >0: inline, -1 = not owned (reference), -2 = owned (heap)
    struct {
        const T* data;
        uint size;
        uint capacity;
    } buffer = {0,0,0};
    /// Number of elements fitting inline
    static constexpr uint32 inline_capacity() { return (sizeof(array)-1)/sizeof(T); }
    /// Data pointer valid while the array is not reallocated (resize or inline array move)
    T* data() { return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
    const T* data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
    /// Number of elements currently in this array
    uint size() const { return tag>=0?tag:buffer.size; }
    /// Sets size without any construction/destruction. /sa resize
    void setSize(uint size) { assert_(size<=capacity()); if(tag>=0) tag=size; else buffer.size=size;}
    /// Maximum number of elements without reallocation (0 for references)
    uint capacity() const { return tag>=0 ? inline_capacity() : buffer.capacity; }

    /// Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    /// \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array)

    /// Default constructs an empty inline array
    array() : tag(0) {}

    /// Move constructor
    array(array&& o) { if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy(*this,o); o.tag=0; }
    /// Move assignment
    array& operator=(array&& o) { this->~array(); if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy(*this,o); o.tag=0; return *this; }
    /// Allocates a new uninitialized array for \a capacity elements
    explicit array(uint capacity) { reserve(capacity); }
    /// Moves elements from a reference
    explicit array(ref<T>&& ref);
    /// Copies elements from a reference
    explicit array(const ref<T>& ref);
    /// References \a size elements from read-only \a data pointer
    array(const T* data, uint size) : i(buffer{data, size, 0}) {} //TODO: escape analysis

    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array();

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity);
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size);
    /// Sets the array size to 0, destroying any contained elements
    void clear();

    /// Returns true if not empty
    explicit operator bool() const { return size(); }
    /// Returns a reference to the elements contained in this array
    operator ref<T>() const { return ref<T>(data(),size()); } //TODO: escape analysis
    /// Appends \a e to array
    array& operator <<(T&& e);
    array& operator <<(array<T>&& a);
    /// Appends only if no matching element is already contained
    array& operator +=(T&& v);
    array& operator +=(array&& b);
    array& operator +=(const T& v);
    array& operator +=(const ref<T>& o);
    /// Compares all elements
    bool operator ==(const ref<T>& b) const { return (ref<T>)*this==b; }

    /// Accessors
    /// \note Use \a ref to avoid inline checking or \a data() to avoid bound checking in performance critical code
    const T& at(uint i) const { assert_(i<size()); return data()[i]; }
    T& at(uint i) { assert_(i<size()); return (T&)data()[i]; }
    const T& operator [](uint i) const { return at(i); }
    T& operator [](uint i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size()-1); }
    T& last() { return at(size()-1); }

    /// Remove elements
    void removeAt(uint i);
    void removeLast();
    T take(int i);
    T takeFirst();
    T takeLast();
    T pop();

    /// Iterators
    const T* begin() const { return data(); }
    const T* end() const { return data()+size(); }
    T* begin() { return (T*)data(); }
    T* end() { return (T*)data()+size(); }
};

#define array array<T>

generic array& operator <<(array& a, T&& e);
generic array& operator <<(array& a, array&& b);

/// Slices a reference to elements from \a pos to \a pos + \a size
generic ref<T> slice(const array& a, uint pos, uint size) { return slice(ref<T>(a),pos,size); }
/// Slices a reference to elements from to the end of the array
generic ref<T> slice(const array& a, uint pos) { return slice(ref<T>(a),pos); }

// Copyable?
/// Appends \a e to array \a a
generic array& operator <<(array& a, const T& e);
/// Appends array \a b to array \a a
generic array& operator <<(array& a, const ref<T>& b);
generic array& operator <<(array& a, const array& b) { return a<<ref<T>(b); }
/// Copies all elements in a new array
generic array copy(const array& a) { return array((ref<T>)a); }
/// Inserts /a e into /a a at /a index
generic T& insertAt(array& a, int index, T&& e);
generic T& insertAt(array& a, int index, const T& e);

// DefaultConstructible?
/// Allocates memory for \a size elements and initializes added elements with their default constructor
generic void grow(array& a, uint size);
/// Sets the array size to \a size, destroying or initializing elements as needed
generic void resize(array& a, uint size);

// Comparable?
generic int indexOf(const array& a, const T& value) { return indexOf(ref<T>(a),value); }
generic bool contains(const array& a, const T& value) { return contains(ref<T>(a),value); }
/// Removes one matching element and returns an index to its successor
generic int removeOne(array& a, T e);
/// Removes all matching elements
generic void removeAll(array& a, T e);
/// Replaces in \a array every occurence of \a before with \a after
generic array replace(array&& a, const T& before, const T& after);

// Orderable?
generic T& min(array& a);
generic T& max(array& a);
generic int insertSorted(array& a, T&& e);
generic int insertSorted(array& a, const T& e);

#undef generic
#undef array

/// \a static_array is an \a array with preallocated inline space
/// \note static_array use static inheritance, instead of using \a array inline space, to avoid full instantiation for each N
template<class T, int N> struct static_array : array<T> {
    no_copy(static_array)
    static_array() { array<T>::tag=-2; array<T>::buffer=typename array<T>::Buffer((T*)buffer,0,N); }
    ubyte buffer[N*sizeof(T)];
};
