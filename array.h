#pragma once
#include "core.h"

namespace std {
template<class E> class initializer_list {
    E* _M_array;
    size_t _M_len;
    constexpr initializer_list(const E* a, size_t l) : _M_array(a), _M_len(l) { }
public:
    constexpr initializer_list() noexcept : _M_array(0), _M_len(0) { }
    constexpr size_t size() const noexcept { return _M_len; }
    constexpr const E* begin() const noexcept { return _M_array; }
    constexpr const E* end() const noexcept { return begin() + size(); }
};
}
using std::initializer_list;

inline uint align(int width, uint offset) { return (offset + (width - 1)) & ~(width - 1); }

/// \a array is a typed and bound-checked handle to a memory buffer (using move semantics)
/// \note array transparently store small arrays inline when possible
/// \note #include "array.cc" to compile arrays or method definitions for custom types
template<class T> struct array {
    /// \a array::Buffer is a lightweight handle to memory (static, stack, heap, mmap...)
    struct Buffer {
        const T* data = 0;
        uint size = 0;
        uint capacity = 0; //0 = not owned
        Buffer(const T* data=0, int size=0, int capacity=0):data(data),size(size),capacity(capacity){}
    };

    int8 tag = -1; //0: empty, >0: inline, -1 = not owned (reference), -2 = owned (heap)
    Buffer buffer;
    static constexpr uint32 inline_capacity() { return (sizeof(array)-1)/sizeof(T); }
    T* data() { return tag>=0? (T*)(&tag+1) : (T*)buffer.data; }
    const T* data() const { return tag>=0? (T*)(&tag+1) : buffer.data; }
    uint size() const { return tag>=0?tag:buffer.size; }
    void setSize(uint size);
    uint capacity() const;

    /// Prevents creation of independent handle, as they might become dangling when this handle free the buffer.
    /// \note Handle to unacquired ressources still might become dangling if the referenced buffer is freed before this handle.
    no_copy(array)

    /// Default constructs an empty inline array
    array() : tag(0) {}

//acquiring constructors
    /// Move constructor
    array(array&& o);
    /// Move assignment
    array& operator=(array&& o);
    /// Allocates a new uninitialized array for \a capacity elements
    explicit array(uint capacity);
    /// Copy elements from an initializer \a list
    array(initializer_list<T>&& list);

//referencing constructors
    /// References \a size elements from read-only \a data pointer
    constexpr array(const T* data, uint size) : buffer(data, size, 0) {}
    /// References elements sliced from \a begin to \a end
    array(const T* begin,const T* end) : buffer(begin, uint(end-begin), 0) {}

    /// If the array own the data, destroys all initialized elements and frees the buffer
    void destroy();
    /// Inline destructor for references
    ~array() { if(tag!=-1) destroy(); }

    /// Allocates enough memory for \a capacity elements
    void reserve(uint capacity);
    /// Sets the array size to \a size and destroys removed elements
    void shrink(uint size);
    /// Sets the array size to 0, destroying any contained elements
    void clear();

    /// Returns true if not empty
    explicit operator bool() const { return size(); }

    /// Accessors
    /// \note array.buffer[i] can be used to avoid inline and bound checking.
    const T& at(uint i) const { debug(if(i>=size())logTrace(),__builtin_abort();) return data()[i]; }
    T& at(uint i) { debug(if(i>=size())logTrace(),__builtin_abort();) return (T&)data()[i]; }
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

    /// Append moveable elements
    void append(T&& v);
    array& operator <<(T&& v);
    void append(array&& a);
    array& operator <<(array&& a);

    /// Iterators
    const T* begin() const { return data(); }
    const T* end() const { return data()+size(); }
    T* begin() { return (T*)data(); }
    T* end() { return (T*)data()+size(); }
};

/// Reinterpret cast \a array to array<T>
template<class T, class O> array<T> cast(array<O>&& array);

#define generic template<class T>
#define array array<T>

/// Slices an array referencing elements from \a pos to \a pos + \a size
/// \note Using move semantics, this operation is safe without refcounting the data buffer
//generic array slice(array&& a, uint pos, uint size);
/// Slices an array referencing elements from \a pos to the end of the array
//generic array slice(array&& a, uint pos);

// Copyable?
/// Slices an array copying elements from \a pos to \a pos + \a size
generic array slice(const array& a, uint pos, uint size);
/// Slices an array copying elements from \a pos to the end of the array
generic array slice(const array& a, uint pos);
/// Append \a v to array \a a
generic array& operator <<(array& a, const T& v);
/// Append array \a b to array \a a
generic array& operator <<(array& a, const array& b);
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
generic bool operator ==(const array& a, const array& b);
generic bool operator !=(const array& a, const array& b);
/// Returns the index of the first occurence of \a value. Returns -1 if \a value could not be found.
generic int indexOf(const array& a, const T& value);
/// Returns true if the array contains an occurrence of \a value
generic bool contains(const array& a, const T& value);
generic int removeOne(array& a, T v);
/// Replaces in \a array every occurence of \a before with \a after
generic array replace(array&& a, const T& before, const T& after);

// Orderable?
generic const T& min(const array& a);
generic T& max(array& a);
generic int insertSorted(array& a, T&& v);
generic int insertSorted(array& a, const T& v);

#undef generic
#undef array
