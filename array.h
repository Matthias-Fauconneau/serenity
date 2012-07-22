#pragma once
#include "core.h"
void unallocate_(byte* buffer, int size);

namespace std {
template<class T> struct initializer_list {
    const T*  data;
    uint size;
    //constexpr initializer_list() : data(0), size(0) {}
    constexpr initializer_list(const T* data, uint size) : data(data), size(size) {}
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
    const T& operator [](uint i) const { assert_(i<size); return data[i]; }
    explicit operator bool() const { return size; }
};
}
/// \a ref is a const typed bounded memory reference (i.e fat pointer)
template<class T> using ref = std::initializer_list<T>;

/// \a array is a typed bounded [growable] memory reference (static, stack, heap, mmap...)
/// \note array use move semantics to avoid reference counting when managing heap reference
/// \note array transparently store small arrays inline (<=15bytes)
/// \note This header only defines basic const container methods, #include "array.cc" to define advanced methods
template<class T> struct array {
    int8 tag = -1; //0: empty, >0: inline, -1 = not owned (reference), -2 = owned (heap)
    struct {
        const T* data;
        uint size;
        uint capacity;
    } buffer = {0,0,0};
    /// Number of elements fitting inline
    static constexpr uint32 inline_capacity() { return (sizeof(array)-1)/sizeof(T); }
    /// Data pointer valid as long as array is not reallocated (resize or inline array move)
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

//acquiring constructors
    /// Move constructor
    array(array&& o) { if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy(*this,o); o.tag=0; }
    /// Move assignment
    array& operator=(array&& o) { this->~array(); if(o.tag<0) tag=o.tag, buffer=o.buffer; else copy(*this,o); o.tag=0; return *this; }
    /// Allocates a new uninitialized array for \a capacity elements
    explicit array(uint capacity) { reserve(capacity); }
    /// References elements from a reference (unsafe if used after ~ref)
    //explicit array(const ref<T>& ref) : i(buffer{ref.data, ref.size, 0}) {}
    /// Moves elements from a reference
    explicit array(ref<T>&& ref);
    /// Copies elements from a reference
    explicit array(const ref<T>& ref);

//referencing constructors
    /// References \a size elements from read-only \a data pointer
    constexpr array(const T* data, uint size) : i(buffer{data, size, 0}) {}
    /// References elements sliced from \a begin to \a end
    array(const T* begin,const T* end) : i(buffer{begin, uint(end-begin), 0}) {}

    /// If the array own the data, destroys all initialized elements and frees the buffer
    ~array() { if(tag!=-1) { for(uint i=0;i<size();i++) at(i).~T(); if(tag==-2) unallocate_((byte*)buffer.data,buffer.capacity*sizeof(T)); } }

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity);
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size);
    /// Sets the array size to 0, destroying any contained elements
    void clear();

    /// Returns true if not empty
    explicit operator bool() const { return size(); }
    /// Returns a lightweight const typed bounded memory reference for operations (unsafe if used after ~array)
    operator ref<T>() const { return ref<T>(data(),size()); }
    /// Inline operators to help disambiguate type deduction
    array<T>& operator <<(T&& v);
    array<T>& operator <<(array<T>&& a);
    array<T>& operator +=(T&& v);
    array<T>& operator +=(array<T>&& a);
    bool operator ==(const ref<T>& r) const;

    /// Accessors
    /// \note to optimize: use \a ref to avoid inline checking or \a data() to avoid bound checking.
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

#define generic template<class T>
#define array array<T>

generic array& operator <<(array& a, T&& v);
generic array& operator <<(array& a, array&& b);

// Slice
/// Slices a reference to elements from \a pos to \a pos + \a size
generic const ref<T> slice(const ref<T>& a, uint pos, uint size) { assert_(pos+size<=a.size); return ref<T>(a.data+pos,size); }
/// Slices a reference to elements from to the end of the array
generic const ref<T> slice(const ref<T>& a, uint pos) { assert_(pos<=a.size); return ref<T>(a.data+pos,a.size-pos); }

/// Slices an array referencing elements from \a pos to \a pos + \a size
/// \note Using move semantics, this operation is safe without refcounting the data buffer
//array slice(array&& a, uint pos, uint size);
/// Slices an array referencing elements from \a pos to the end of the array
//array slice(array&& a, uint pos);

// Copyable?
/// Slices an array copying elements from \a pos to \a pos + \a size
//generic array slice(const array& a, uint pos, uint size);
/// Slices an array copying elements from \a pos to the end of the array
//generic array slice(const array& a, uint pos);
/// Append \a v to array \a a
generic array& operator <<(array& a, const T& v);
/// Append array \a b to array \a a
generic array& operator <<(array& a, const ref<T>& b);
generic array& operator <<(array& a, const array& b) { return a<<ref<T>(b); }
/// Copies all elements in a new array
generic array copy(const array& a);
/// Inserts /a v into /a a at /a index
generic T& insertAt(array& a, int index, T&& v);
generic T& insertAt(array& a, int index, const T& v);

// DefaultConstructible?
/// Allocates memory for \a size elements and initializes added elements with their default constructor
generic void grow(array& a, uint size);
/// Sets the array size to \a size, destroying or initializing elements as needed
generic void resize(array& a, uint size);

// Comparable?
/// Compares all elements
generic bool operator ==(const ref<T>& a, const ref<T>& b);
/// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
generic int indexOf(const ref<T>& a, const T& value);
/// Returns true if the array contains an occurrence of \a value
generic bool contains(const ref<T>& a, const T& value);
/// Removes one matching element and returns an index to its successor
generic int removeOne(array& a, T v);
/// Removes all matching elements
generic void removeAll(array& a, T v);
/// Appends only if no matching element is already contained
generic array& operator +=(array& s, const T& t);
generic array& operator +=(array& s, const array& o);
/// Replaces in \a array every occurence of \a before with \a after
generic array replace(array&& a, const T& before, const T& after);

// Orderable?
generic const T& min(const array& a);
generic T& max(array& a);
generic int insertSorted(array& a, T&& v);
generic int insertSorted(array& a, const T& v);

/// Overloads to help implicit array to ref conversion
generic inline array& array::operator <<(T&& v) { return ::operator<<(*this, move(v)); }
generic inline array& array::operator <<(array&& a) { return ::operator<<(*this, move(a)); }
generic inline array& array::operator +=(T&& v) { return ::operator+=(*this, v); }
generic inline array& array::operator +=(array&& a) { return ::operator+=(*this, a); }
generic inline bool  array::operator ==(const ref<T>& r) const { return ::operator==(ref<T>(*this), r); }
generic inline const ref<T> slice(const array& a, uint pos, uint size) { return slice(ref<T>(a),pos,size); }
generic inline const ref<T> slice(const array& a, uint pos) { return slice(ref<T>(a),pos); }

generic int indexOf(const array& a, const T& value) { return indexOf(ref<T>(a),value); }
generic bool contains(const array& a, const T& value) { return contains(ref<T>(a),value); }

#undef generic
#undef array
