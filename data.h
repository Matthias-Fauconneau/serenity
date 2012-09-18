#pragma once
#include "array.h"

/// Casts raw memory to \a T
template<class T> const T& raw(const ref<byte>& a) { assert_(a.size==sizeof(T)); return *(T*)a.data; }
/// Casts between element types
template<class T, class O> ref<T> cast(const ref<O>& o) {
    assert_((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// \a Data is an interface to read structured data (\sa BinaryData TextData)
/// \note \a available can be overriden to feed \a buffer as needed (\sa DataStream)
struct Data {
    array<byte> buffer;
    uint index=0;
    default(Data)
    /// Creates a Data interface to an \a array
    Data(array<byte>&& array) : buffer(move(array)) {}
    /// Creates a Data interface to a \a reference
    explicit Data(const ref<byte>& reference) : buffer(reference.data,reference.size) {}
    /// Slices a reference to the buffer from \a pos to \a pos + \a size
    ref<byte> slice(uint pos, uint size) const { return buffer.slice(pos,size); }

    /// Buffers \a need bytes (if overriden) and returns number of bytes available
    virtual uint available(uint /*need*/) { return buffer.size()-index; }
    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }

    /// Returns next byte without advancing
    byte peek() const { assert_(index<buffer.size()); return buffer[index]; }
    /// Returns a reference to the  next \a size bytes
    ref<byte> get(uint size) const { return slice(index,size); }

    /// Advances \a count bytes
    void advance(uint count) {assert_(index+count<=buffer.size()); index+=count; }
    /// Returns next byte and advance one byte
    byte next() { byte b=peek(); advance(1); return b; }
    /// Returns a reference to the next \a size bytes and advances \a size bytes
    ref<byte> read(uint size) { ref<byte> t = get(size); advance(size); return t; }
};

#define big32 __builtin_bswap32
inline uint16 __builtin_bswap16(uint16 x) { return (x<<8)|(x>>8); }
#define big16 __builtin_bswap16

/// \a BinaryData provides a convenient interface to parse binary inputs
struct BinaryData : virtual Data {
    bool isBigEndian = false;
    default_move(BinaryData) move_operator(BinaryData)
    /// Creates a BinaryData interface to an \a array
    BinaryData(array<byte>&& array, bool isBigEndian=false) : Data(move(array)), isBigEndian(isBigEndian) {}
    /// Creates a BinaryData interface to a \a reference
    explicit BinaryData(const ref<byte>& reference, bool isBigEndian=false):Data(reference),isBigEndian(isBigEndian){}

    /// Slices a reference to the buffer from \a pos to \a pos + \a size
    BinaryData slice(uint pos, uint size) { return BinaryData(Data::slice(pos,size),isBigEndian); }
    /// Seeks to /a index
    void seek(uint index) { assert_(index<buffer.size()); this->index=index; }
    /// Seeks last match for \a key.
    bool seekLast(const ref<byte>& key);

    /// Reads until next null byte
    ref<byte> untilNull();

    /// Reads one raw \a T element
    template<class T> const T& read() { const T& t = raw<T>(Data::read(sizeof(T))); return t; }
    int32 read32() { return isBigEndian?big32(read<int32>()):read<int32>(); }
    int16 read16() { return isBigEndian?big16(read<int16>()):read<int16>(); }

    /// Provides template overloaded specialization (for swap) and return type overloading through cast operators.
    struct ReadOperator {
        BinaryData * s;
        /// Reads an int32 and if necessary, swaps to host byte order
        operator uint32() { return s->read32(); }
        operator int32() { return s->read32(); }
        /// Reads an int16 and if necessary, swaps to host byte order
        operator uint16() { return s->read16(); }
        operator int16() { return s->read16(); }
        /// Reads an int8
        operator uint8() { return s->read<uint8>(); }
        operator int8() { return s->read<int8>(); }
        operator byte() { return s->next(); }
    };
    ReadOperator read() { return __(this); }

    /// Reads \a size raw \a T elements
    template<class T>  ref<T> read(uint size) { return cast<T>(Data::read(size*sizeof(T))); }

    /// Provides return type overloading for reading arrays (swap as needed)
    struct ArrayReadOperator {
       BinaryData* s; uint size;
       template<class T> operator array<T>() { array<T> t; for(uint i=0;i<size;i++) t<<(T)s->read(); return t; }
   };
   ArrayReadOperator read(uint size) { return __(this,size); }

   /// Reads \a size \a T elements (swap as needed)
   template<class T>  void read(T buffer[], uint size) { for(uint i=0;i<size;i++) buffer[i]=(T)read(); }
};

/// \a TextData provides a convenient interface to parse text streams
struct TextData : virtual Data {
    default_move(TextData) move_operator(TextData)
    /// Creates a TextData interface to an \a array
    TextData(array<byte>&& array) : Data(move(array)){}
    /// Creates a TextData interface to a \a reference
    explicit TextData(const ref<byte>& reference):Data(reference){}

    /// If input match \a key, advances \a pos by \a key size
    bool match(char key);
    /// If input match \a key, advances \a pos by \a key size
    bool match(const ref<byte>& key);
    /// If input match any of \a key, advances \a pos
    bool matchAny(const ref<byte>& any);
    /// If input match none of \a key, advances \a pos
    bool matchNo(const ref<byte>& any);

    /// advances \a pos while input doesn't match \a key
    /// \sa until
    ref<byte> whileNot(char key);
    /// advances \a pos while input match any of \a key
    ref<byte> whileAny(const ref<byte>& key);
    /// advances \a pos while input match none of \a key
    ref<byte> whileNo(const ref<byte>& key);

    /// Reads until input match \a key
    /// \sa whileNot
    ref<byte> until(char key);
    /// Reads until input match \a key
    ref<byte> until(const ref<byte>& key);
    /// Reads until input match any character of \a key
    ref<byte> untilAny(const ref<byte>& any);
    /// Reads until the end of input
    ref<byte> untilEnd();
    /// Skips whitespaces
    void skip();
    /// Reads a single word
    ref<byte> word();
    /// Reads a single identifier [a-zA-Z0-9_-:]*
    ref<byte> identifier();
    /// Reads a single number
    int number(uint base=10);
    /// Reads one possibly escaped character
    char character();
};
