#pragma once
#include "array.h"
#include "string.h"
#include "debug.h"

/// create an array<byte> reference of \a t raw memory representation
template<class T> inline array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }
/// cast raw memory to a \a T
template<class T> inline T raw(const array<byte>& a) { assert(a.size()==sizeof(T)); return *(T*)a.data(); }

/// \a Stream is an interface used by \a DataStream, TextStream and implemented by Buffer, File, Socket
struct Stream {
    /// Returns number of bytes available, reading \a need bytes from underlying device if possible
    virtual uint available(uint need) = 0;
    /// Returns next \a size bytes from stream
    virtual array<byte> get(uint size) = 0;
    /// Advances \a count bytes in stream
    virtual void advance(int count) = 0;

    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }
};

/// Buffer is an in-memory \a Stream
struct Buffer : virtual Stream {
    array<byte> buffer;
    uint index=0;

    Buffer(){}
    Buffer(Buffer&& o):buffer(move(o.buffer)),index(o.index){}
    /// Returns a Buffer to stream /a array
    Buffer(array<byte>&& array) : buffer(move(array)) {}

    /// Seeks stream to /a index
    void seek(uint index) { assert(index<buffer.size()); this->index=index; }

    /// Seeks last match for \a key.
    bool seekLast(const array<byte>& key) {
        for(index=buffer.size()-key.size();index>0;index--) { if(get(key.size()) == key) return true; }
        return false;
    }

    uint available(uint) override { return buffer.size()-index; }
    array<byte> get(uint size) override { assert(index+size<=buffer.size());  return array<byte>(buffer.data()+index,size); }
    void advance(int count) override { index+=count;  assert(index<=buffer.size()); }
};

#define swap32 __builtin_bswap32
inline uint16 swap16(uint16 x) { return swap32(x)>>16; }

/// \a DataStream provides a convenient interface to parse binaries
struct DataStream : virtual Stream {
    bool bigEndian = false;

    /// Reads one raw \a T element from stream
    template<class T> T read() { T t = raw<T>(get(sizeof(T))); advance(sizeof(T)); return t; } //inline for custom types
    /// Reads \a size raw \a T elements from stream
    //template<class T> array<T> read(uint size) { array<T> t = cast<T>(get(size*sizeof(T))); advance(size*sizeof(T)); return t; }
    array<byte> read(uint size) { array<byte> t = get(size); advance(size); return t; }
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    //template<class T> array<T> readArray() { uint size=read<uint32>(); return read<T>(size); }
    array<byte> readArray() { uint size=read<uint32>(); return read(size); }
    /// Read until the end of stream
    array<byte> readAll() { uint size=available(-1); return read(size); }

    struct ReadOperator {
        DataStream * s;
        /// Reads an int32 and if necessary, swaps from stream to host byte order
        operator uint32() { return s->bigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
        operator int32() { return operator uint32(); }
        /// Reads an int16 and if necessary, swaps from stream to host byte order
        operator uint16() { return s->bigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
        operator int16() { return operator uint16(); }
        /// Reads a \a T element (deduced from the destination (return type overload))
        template<class T> operator T() { return s->read<T>(); }
    };
    /// Returns an operator to deduce the type to read from the destination (return type overload)
    ReadOperator read() { return {this}; }
};

/// \a TextStream provides a convenient interface to parse texts
struct TextStream : virtual Stream {
    /// Reads \a size bytes from stream
    array<byte> read(uint size) { array<byte> t = get(size); advance(size); return t; }

    /// If stream match \a key, advances \a pos by \a key size
    bool match(const string& key);
    /// If stream match any of \a key, advances \a pos
    bool matchAny(const array<byte>& any);
    /// advances \a pos while stream match any of \a key
    void whileAny(const array<byte>& any);
    /// Reads until stream match \a key
    string until(const string& key);
    /// Reads until stream match any character of \a key
    string untilAny(const array<byte>& any);
    /// Reads until the end of stream
    string untilEnd();
    /// Skips whitespaces
    void skip();
    /// Reads a single word
    string word();
    /// Reads a single XML identifier
    string xmlIdentifier();
    /// Reads a single number
    int number();
};

struct DataBuffer : DataStream, Buffer {
    DataBuffer(array<byte>&& buffer):Buffer(move(buffer)){}
};

struct TextBuffer : TextStream, Buffer {
    TextBuffer(array<byte>&& buffer):Buffer(move(buffer)){}
};
