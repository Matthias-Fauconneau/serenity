#pragma once
#include "core.h"
#include <initializer_list>

/// Returns the next \a offset aligned to \a width
template<int width> inline int align(int offset) {
    static_assert(width && !(width & (width - 1)),"width must be a power of two");
    return (offset + width - 1) & ~(width - 1);
}

/// \a array is a typed and bound-checked handle to memory using move semantics to avoid reference counting
template <class T> struct array {
//read:
    const T* data = 0;
    int size = 0;
    int capacity = 0; //0 = not owned

    /// Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    /// \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array)

    /// Default constructs an empty array
    array(){}

//acquiring constructors
    /// Move constructor
    array(array&& o) : data(o.data), size(o.size), capacity(o.capacity) { o.capacity=0; }
    /// Move constructor with conversion
    template <class O> explicit array(array<O>&& o)
        : data((const T*)&o), size(o.size*sizeof(O)/sizeof(T)), capacity(o.capacity*sizeof(O)/sizeof(T)) { o.capacity=0; }
    /// Move assigment
    array& operator=(array&& o) { this->~array(); data=o.data; size=o.size; capacity=o.capacity; o.capacity=0; return *this; }

    /// Allocates a new uninitialized array for \a capacity elements
    /// \note use placement new to safely initialize objects with assignment operator
    explicit array(int capacity) : data(allocate<T>(capacity)), capacity(capacity) { assert_(capacity>0); }
    /// Allocates a new array with \a size elements initialized to \a value
    array(int size, const T& value) : data((T*)malloc(size*sizeof(T))), capacity(size) { for(int i=0;i<size;i++) append(value); }
    /// Copy elements from an initializer \a list
    array(const std::initializer_list<T>& list) { append(array((T*)list.begin(),list.size())); }

//referencing constructors
    /// References \a size elements from \a data pointer
    array(const T* data, int size) : data(data), size(size) { assert_(size>=0); assert_(data); }
    /// References elements sliced from \a begin to \a end
    array(const T* begin,const T* end) : data(begin), size(int(end-begin)) { assert_(size>=0); assert_(data); }

    /// if \a this own the data, destroys all initialized elements and frees the buffer
    ~array() { if(capacity) { for(int i=0;i<size;i++) data[i].~T(); free((void*)data); } }

    /// Allocates enough memory for \a capacity elements
    void reserve(int capacity) {
        if(this->capacity>=capacity) return;
        if(this->capacity) data=(T*)realloc((void*)data,(size_t)capacity*sizeof(T));
        else if(capacity) { T* detach=allocate<T>(capacity); copy((byte*)detach,(byte*)data,size*sizeof(T)); data=detach; }
        this->capacity=capacity;
    }
    /// Sets the array size to \a size and destroys removed elements
    void shrink(int size) { assert_(size<this->size); if(capacity) for(int i=size;i<this->size;i++) data[i].~T(); this->size=size; }
    /// Allocates memory for \a size elements and initializes added elements with their default constructor
    void grow(int size) { assert_(size>this->size); reserve(size); for(int i=this->size;i<size;i++) new ((void*)(data+i)) T(); this->size=size; }
    /// Sets the array size to \a size, destroying or initializing elements as needed
    void resize(int size) { if(size<this->size) shrink(size); else if(size>this->size) grow(size); }
    /// Sets the array size to 0, destroying any contained elements
    void clear() { if(size) shrink(0); }

    /// Returns a raw pointer to \a data buffer
    T const * const & operator &() const { return data; }
    /// Returns true if not empty
    explicit operator bool() const { return size; }

    /// Accessors
    const T& at(int i) const { assert_(i>=0 && i<size); return data[i]; }
    T& at(int i) { assert_(i>=0 && i<size); return (T&)data[i]; }
    const T& operator [](int i) const { return at(i); }
    T& operator [](int i) { return at(i); }
    const T& first() const { return at(0); }
    T& first() { return at(0); }
    const T& last() const { return at(size-1); }
    T& last() { return at(size-1); }

    /// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
    int indexOf(const T& value) const { for(int i=0;i<size;i++) { if(data[i]==value) return i; } return -1; }
    /// Returns true if the array contains an occurrence of \a value
    bool contains(const T& value) const { return indexOf(value)>=0; }
    /// Searches for an element using a comparator delegate
    template<class Comparator> int find(Comparator lambda) { for(int i=0;i<size;i++) { if(lambda(at(i))) return i; } return -1; }

    /// remove
    void removeAt(int i) { assert_(i>=0 && i<size); for(;i<size-1;i++) copy((byte*)&at(i),(byte*)&at(i+1),sizeof(T)); size--; }
    void removeRef(T* p) { assert_(p>=data && p<data+size); removeAt(p-data); }
    void removeLast() { assert_(size); removeAt(size-1); }
    void removeOne(T v) { int i=indexOf(v); if(i>=0) removeAt(i); }
    T take(int i) { T value = move(at(i)); removeAt(i); return value; }
    T takeFirst() { return take(0); }
    T takeLast() { return take(size-1); }
    T pop() { return takeLast(); }

    /// append element
    void append(T&& v) { int s=size+1; reserve(s); new (end()) T(move(v)); size=s; }
    void append(const T& v) { int s=size+1; reserve(s); new (end()) T(copy(v)); size=s; }
    array& operator <<(T&& v) { append(move(v)); return *this; }
    array& operator <<(const T&  v) { append(v); return *this; }
    //template<perfect(T)> void appendOnce(Tf&& v) { if(!contains(v)) append(forward<Tf>(v)); }

    /// append array
    void append(array&& a) { int s=size+a.size; reserve(s); copy((byte*)end(),(byte*)a.data,a.size*sizeof(T)); size=s; }
    void append(const array& a) { int s=size+a.size; reserve(s); for(const auto& e: a) new ((void*)(data+size++)) T(copy(e)); }
    array& operator <<(array&& a) { append(move(a)); return *this; }
    array& operator <<(const array& a) { append(a); return *this; }

    void fill(const T& value, int size) { resize(size); for(auto& e: *this) e=copy(value); }

    /// convenience methods for data serialization
    void skip(int skip) { int s=size+skip; reserve(s); for(;size<s;size++) new (end()) T(); }
    template <int width> void align() { int s=::align<width>(size); reserve(s); for(;size<s;size++) new (end()) T(); }

    /// insert
    template<perfect(T)> void insertAt(int index, Tf&& v) {
        reserve(size+1); size++;
        for(int i=size-2;i>=index;i--) copy((byte*)&at(i+1),(byte*)&at(i),sizeof(T));
        new (addressof(at(index))) T(forward<Tf>(v));
    }
    template<perfect(T)> void insertSorted(Tf&& v) {
        int i=0;
        for(;i<size;i++) if(v < at(i)) break;
        insertAt(i,forward<Tf>(v));
    }
    void insertSorted(array&& a) {
        int s=size+a.size; reserve(s);
        for(auto&& e: a) insertSorted(move(e));
    }

    /// iterators
    const T* begin() const { return data; }
    const T* end() const { return data+size; }
    T* begin() { return (T*)data; }
    T* end() { return (T*)data+size; }
};

/// comparison
template<class T> bool operator ==(const array<T>& a, const array<T>& b) {
    if(a.size != b.size) return false;
    for(int i=0;i<a.size;i++) if(!(a[i]==b[i])) return false;
    return true;
}
template<class T> bool operator !=(const array<T>& a, const array<T>& b) { return !(a==b); }

/// Copies all elements in a new array
template<class T> array<T> copy(const array<T>& a) { array<T> r; r<<a; return  r; }

/// Slices an array referencing elements from \a pos to \a pos + \a size
/// \note Using move semantics, this operation is safe without refcounting the data buffer
template<class T> array<T> slice(array<T>&& a, int pos,int size) {
    assert_(pos>=0 && pos+size<=a.size);
    assert_(a.capacity == 0); //only allow slicing of referencing arrays. TODO: custom allocator allowing arbitrary resize
    return array<T>(a.data+pos,size);
}
/// Slices an array referencing elements from \a pos to the end of the array
template<class T> array<T> slice(array<T>&& a, int pos) { return slice(move(a),pos,a.size-pos); }

/// Slices an array copying elements from \a pos to \a pos + \a size
template<class T> array<T> slice(const array<T>& a, int pos,int size) {
    assert_(pos>=0 && pos+size<=a.size);
    return copy(array<T>(a.data+pos,size));
}
/// Slices an array copying elements from \a pos to the end of the array
template<class T> array<T> slice(const array<T>& a, int pos) { return slice(a,pos,a.size-pos); }

/// Reverses elements in-place
template<class T> array<T> reverse(array<T>&& a) { for(int i=0; i<a.size/2; i++) swap(a[i], a[a.size-i-1]); return move(a); }
/// Constructs a copy with reversed elements
template<class T> array<T> reverse(const array<T>& a) { array<T> r(a.size); r.size=a.size; for(int i=0;i<a.size;i++) new((T*)&r+a.size-1-i) T(copy(a[i])); return r; }

/// Replaces every occurence of \a before replaced with \a after in \a array
template<class T> array<T>  replace(array<T>&& array, const T& before, const T& after) { for(auto& e : array) if(e==before) e=copy(after); return array; }
/// Constructs a copy of \a array with every occurence of \a before replaced with \a after
template<class T> array<T>  replace(const array<T>& a, const T& before, const T& after) {
    array<T> r(a.size); r.size=a.size; for(int i=0;i<a.size;i++) new ((T*)&r+i) T(copy(a[i]==before? after : a[i])); return r;
}

template<class T> const T& min(const array<T>& a) { T* min=&a.first(); for(T& e: a) if(e<*min) min=&e; return *min; }
template<class T> T& max(array<T>& a) { T* max=&a.first(); for(T& e: a) if(e>*max) max=&e; return *max; }
template<class T> T sum(const array<T>& a) { T sum=0; for(const T& e: a) sum+=e; return sum; }
