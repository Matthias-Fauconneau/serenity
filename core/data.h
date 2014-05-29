#pragma once
/// \file data.h Structured data parsers (Data, BinaryData, TextData)
#include "memory.h"

inline uint16 bswap(uint16 v) { return __builtin_bswap16(v); }
inline uint32 bswap(uint32 v) { return __builtin_bswap32(v); }
inline uint64 bswap(uint64 v) { return __builtin_bswap64(v); }
inline int16 bswap(int16 v) { return __builtin_bswap16(v); }
inline int32 bswap(int32 v) { return __builtin_bswap32(v); }
inline int64 bswap(int64 v) { return __builtin_bswap64(v); }
//inline float bswap(float v) { return __builtin_bswap32(v); }

/// Aligns \a offset to \a width (only for power of two \a width)
inline uint align(uint width, uint offset) { assert((width&(width-1))==0); return (offset + (width-1)) & ~(width-1); }

/// Reinterpret cast a const reference to another type
template<Type T, Type O> ref<T> cast(const ref<O>& o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret cast a mutable reference to another type
template<Type T, Type O> mref<T> mcast(const mref<O>& o) {
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    return mref<T>((T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// Reinterpret cast a buffer to another type
template<Type T, Type O> buffer<T> cast(buffer<O>&& o) {
    buffer<T> buffer;
    buffer.data = (const T*)o.data;
    assert((o.size*sizeof(O))%sizeof(T) == 0);
    buffer.size = o.size*sizeof(O)/sizeof(T);
    assert((o.capacity*sizeof(O))%sizeof(T) == 0);
    buffer.capacity = o.capacity*sizeof(O)/sizeof(T);
    o.capacity = 0;
    return buffer;
}

generic inline buffer<T> bswap(const ref<T>& data) {
    ::buffer<T> buffer(data.size);
    for(uint i: range(data.size)) buffer[i] = bswap(data[i]);
    return buffer;
}

/// Interface to read structured data. \sa BinaryData TextData
/// \note \a available can be overridden to feed \a buffer as needed. \sa DataStream
struct Data {
    Data(){}
    /// Creates a Data interface to an \a array
    Data(::buffer<byte>&& array) : buffer(move(array)) {}
    /// Creates a Data interface to a \a reference
    explicit Data(const ref<byte>& reference) : buffer(reference.data,reference.size) {}
    /// Slices a reference to the buffer from \a pos to \a pos + \a size
    ref<byte> slice(uint pos, uint size) const { return buffer.slice(pos,size); }
    /// Slices a reference to the buffer from \a pos to the end of the buffer
    ref<byte> slice(uint pos) const { return buffer.slice(pos); }

    /// Buffers \a need bytes (if overridden) and returns number of bytes available
    virtual uint available(uint /*need*/) { return buffer.size-index; }
    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }

    /// Returns next byte without advancing
    byte peek() const { assert(index<buffer.size, index, buffer.size); return buffer[index];}
    /// Peeks at buffer without advancing
    byte operator[](int i) const { assert(index+i<buffer.size); return buffer[index+i]; }
    /// Returns a reference to the next \a size bytes
    ref<byte> peek(uint size) const { return slice(index,size); }

    /// Advances \a count bytes
    void advance(uint step) {assert(index+step<=buffer.size,index,step,buffer.size); index+=step; }
    /// Returns next byte and advance one byte
    byte next() { byte b=peek(); advance(1); return b; }
    /// Returns a reference to the next \a size bytes and advances \a size bytes
    ref<byte> read(uint size) { ref<byte> t = peek(size); advance(size); return t; }

    /// Reads until the end of input
    ref<byte> untilEnd() { uint size=available(-1); return read(size); }

    ::buffer<byte> buffer;
    uint index=0;
};

/// Provides a convenient interface to parse binary inputs
struct BinaryData : Data {
    BinaryData(){}
    /// Creates a BinaryData interface to an \a array
    BinaryData(::buffer<byte>&& buffer, bool isBigEndian=false) : Data(move(buffer)), isBigEndian(isBigEndian) {}
    /// Creates a BinaryData interface to a \a reference
    explicit BinaryData(const ref<byte>& reference, bool isBigEndian=false):Data(reference),isBigEndian(isBigEndian){}

    /// Slices a reference to the buffer from \a pos to \a pos + \a size
    BinaryData slice(uint pos, uint size) { return BinaryData(Data::slice(pos,size),isBigEndian); }
    /// Slices a reference to the buffer from \a pos to \a pos + \a size
    BinaryData slice(uint pos) { return BinaryData(Data::slice(pos),isBigEndian); }

    /// Seeks to /a index
    void seek(uint index) { assert(index<buffer.size); this->index=index; }
    /// Seeks last match for \a key.
    bool seekLast(const ref<byte>& key);
    /// Seeks to next aligned position
    void align(uint width) { index=::align(width,index); }

    /// Reads until next null byte
    ref<byte> untilNull();

    /// Reads one raw \a T element
    generic const T& read() { return *(T*)Data::read(sizeof(T)).data; }
    int64 read64() { return isBigEndian?bswap(read<int64>()):read<int64>(); }
    int32 read32() { return isBigEndian?bswap(read<int32>()):read<int32>(); }
    int16 read16() { return isBigEndian?bswap(read<int16>()):read<int16>(); }

    /// Provides template overloaded specialization (for swap) and return type overloading through cast operators.
    struct ReadOperator {
        BinaryData * s;
        /// Reads an int64 and if necessary, swaps to host byte order
        operator uint64() { return s->read64(); }
        operator int64() { return s->read64(); }
        /// Reads an int32 and if necessary, swaps to host byte order
        operator uint32() { return s->read32(); }
        operator int32() { return s->read32(); }
        /// Reads an int16 and if necessary, swaps to host byte order
        operator uint16() { return s->read16(); }
        operator int16() { return s->read16(); }
        /// Reads an int8
        operator uint8() { return s->read<uint8>(); }
        operator int8() { return s->read<int8>(); }
    };
    ReadOperator read() { return {this}; }

    /// Reads \a size raw \a T elements
    generic ref<T> read(uint size) { return cast<T>(Data::read(size*sizeof(T))); }

    /// Provides return type overloading for reading arrays (swap as needed)
    struct ArrayReadOperator {
       BinaryData* s; uint size;
       generic operator ::buffer<T>() { ::buffer<T> buffer(size); for(uint i: range(size)) new (&buffer.at(i)) T(s->read()); return buffer; }
   };
   ArrayReadOperator read(uint size) { return {this,size}; }

   /// Reads \a size \a T elements (swap as needed)
   generic  void read(T buffer[], uint size) { for(uint i: range(size)) buffer[i]=(T)read(); }

   bool isBigEndian = false;
};

/// Provides a convenient interface to parse text streams
struct TextData : Data {
    using Data::Data;
    void advance(uint step) /*override*/;

    /// If input match \a key, advances \a pos by \a key size
    bool match(char key);
    /// If input match \a key, advances \a pos by \a key size
    bool match(const string& key);
    /// If input match any of \a key, advances \a pos
    bool matchAny(const string& any);
    /// If input match none of \a key, advances \a pos
    bool matchNo(const string& any);

    /// Asserts stream matches \a key and advances \a key length bytes
    void skip(const string& key);

    /// Advances while input doesn't match \a key. \sa until
    string whileNot(char key);
    /// Advances while input match any of \a any
    string whileAny(const string& any);
    /// Advances while input match none of \a any
    string whileNo(const string& any);
    /// Advances while input match none of \a any or \a right (which may be nested using \a left)
    string whileNo(const string& any, char left, char right);

    /// Reads until input match \a key. \sa whileNot
    string until(char key);
    /// Reads until input match \a key
    string until(const string& key);
    /// Reads until input match any character of \a key
    string untilAny(const string& any);
    /// Skips whitespaces
    void skip();
    /// Reads until end of line
    string line();
    /// Reads one possibly escaped character
    char character();
    /// Reads a word [a-zA-Z/special/]+
    string word(const string& special=""_);
    /// Reads a identifier [a-zA-Z0-9/special/]*
    string identifier(const string& special=""_);
    /// Matches [-+]?[0-9]*
    string whileInteger(bool sign=false);
    /// Reads an integer
    int integer(bool sign=false);
    /// Reads a signed integer, return defaultValue if fails
    int mayInteger(int defaultValue=-1);
    /// Matches [0-9a-fA-F]*
    string whileHexadecimal();
    /// Reads an hexadecimal integer
    uint hexadecimal();
    /// Matches [-+]?[0-9]*\.[0-9]*
    string whileDecimal();
    /// Reads a decimal number
    double decimal();

    uint lineIndex = 1;
};
