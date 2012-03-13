#pragma once
#include "array.h"

constexpr uint16 swap16(uint16 x) { return ((x>>8)&0xff)|((x&0xff)<<8); }
#define swap32 __builtin_bswap32

/// create an array<byte> reference of a types's raw memory representation
template<class T> array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }

/// \a Stream provides a convenient interface for reading binary data or text
struct Stream {
    array<byte> buffer;
    const byte* data;
    uint index;
    bool bigEndian;
    Stream(array<byte>&& buffer);

    /// Returns true if there is data to read
    explicit operator bool() const;

    /// Peeks at the next raw \a T element in stream without moving \a pos
    template<class T> const T& peek() const;
    /// Peeks at the next \a size raw \a T elements in stream without moving \a pos
    template<class T> array<T> peek(int size) const;


    /// Reads one raw \a T element from stream
    template<class T> T read();
    /// Reads \a size raw \a T elements from stream
    template<class T> array<T> read(int size);
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    template<class T> array<T> readArray();

    struct ReadOperator {
        Stream * s;
        /// Reads an int32 and if necessary, swaps from stream to host byte order
        operator int32();
        operator uint32();
        /// Reads an int16 and if necessary, swaps from stream to host byte order
        operator int16();
        operator uint16();
        /// Reads a \a T element (deduced from the destination (return type overload))
        template<class T> operator T();
    };
    /// Returns an operator to deduce the type to read from the destination (return type overload)
    ReadOperator read();

    /// Skips a byte from the stream
    Stream& operator ++(int);
    /// Skips \a count byte from the stream
    Stream& operator +=(int count);

    /// If stream match any of \a key, advances \a pos
    template<class T> bool matchAny(const array<T>& any);
    /// If stream match \a key, advances \a pos by \a key size
    template<class T> bool match(const array<T>& key);

    /// advances \a pos until stream match \a key
    /// \returns no match count
    template<class T> array<T> until(const T& key);

    /// advances \a pos while stream match any of \a key
    /// \returns reference to matched data
    template<class T> array<T> whileAny(const array<T>& any);

    /// advances \a pos until stream match any of \a key
    /// \returns no match count
    template<class T> array<T> untilAny(const array<T>& any);
};
