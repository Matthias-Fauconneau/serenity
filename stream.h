#pragma once
#include "array.h"
#include "string.h"

/// create an array<byte> reference of \a t raw memory representation
template<class T> array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }
/// cast raw memory to a \a T
template<class T> T raw(const array<byte>& a) { assert(a.size()==sizeof(T)); return *(T*)a.data(); }

/// \a Stream is an abstract interface used by \a DataStream, TextStream and implemented by Buffer,File,Socket
struct Stream {
    /// Returns number of bytes available, reading \a need bytes from underlying device if possible
    virtual uint available(uint need) = 0;
    virtual array<byte> peekData(uint size) = 0;
    virtual void advance(int count) = 0;
};

/// \a Stream provides a convenient interface for reading binary data
struct DataStream : virtual protected Stream {
    bool bigEndian = false;

    /// Returns true if there is data to read
    explicit operator bool();

    /// Peeks at the next raw \a T element in stream without moving \a pos
    template<class T> const T& peek();
    /// Peeks at the next \a size raw \a T elements in stream without moving \a pos
    template<class T> array<T> peek(uint size);

    /// Reads one raw \a T element from stream
    template<class T> T read();
    /// Reads \a size raw \a T elements from stream
    template<class T> array<T> read(uint size);
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    template<class T> array<T> readArray();
    /// Reads the rest of the stream as raw \a T elements
    template<class T> array<T> readAll();

    struct ReadOperator {
        DataStream * s;
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

    /// If stream match any of \a key, advances \a pos
    template<class T> bool matchAny(const array<T>& any);
    /// If stream match \a key, advances \a pos by \a key size
    template<class T> bool match(const array<T>& key);

    /// advances \a pos until stream match \a key
    /// \returns no match count
    template<class T> array<T> until(const T& key);

    /// advances \a pos until stream match \a key
    /// \returns no match count
    template<class T> array<T> until(const array<T>& key);

    /// advances \a pos while stream match any of \a key
    template<class T> void whileAny(const array<T>& any);

    /// advances \a pos until stream match any of \a key
    /// \returns no match count
    template<class T> array<T> untilAny(const array<T>& any);
};

/// Buffer is an in-memory \a Stream
struct Buffer : virtual Stream {
    array<byte> buffer;
    uint index=0;
    Buffer(){}
    Buffer(array<byte>&& buffer);
    uint available(uint) override;
    array<byte> peekData(uint size) override;
    void advance(int count) override;
};

/// \a TextStream is a \a DataStream with convenience methods for text
//TODO: regexp
struct TextStream : DataStream {
    /// Skips whitespaces
    void skip();
    string readAll() { return DataStream::readAll<char>(); }
    string until(const string& key);
    string word();
    string xmlIdentifier();
};

struct DataBuffer : DataStream, Buffer {
    DataBuffer(array<byte>&& buffer):Buffer(move(buffer)){}
};

struct TextBuffer : TextStream, Buffer {
    TextBuffer(array<byte>&& buffer):Buffer(move(buffer)){}
};
