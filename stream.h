#pragma once
#include "array.h"

/// Returns padding zeroes to append in order to align an array of \a size bytes to \a width
inline ref<byte> pad(uint width, uint size){ static byte zero[4]={}; assert_(width<=sizeof(zero)); return ref<byte>(zero,align(width,size)-size); }

/// References raw memory representation of \a t
template<class T> ref<byte> raw(const T& t) { return ref<byte>((byte*)&t,sizeof(T)); }
/// Casts raw memory to \a T
template<class T> const T& raw(const ref<byte>& a) { assert_(a.size==sizeof(T)); return *(T*)a.data; }
/// Casts between element types
template<class T, class O> ref<T> cast(const ref<O>& o) {
    assert_((o.size*sizeof(O))%sizeof(T) == 0);
    return ref<T>((const T*)o.data,o.size*sizeof(O)/sizeof(T));
}

/// \a InputData is an interface to read structured data (\sa BinaryData TextData) from streams (\sa Socket) or buffers
struct InputData {
    const array<byte> buffer;
    uint index=0;
    void invariant(){assert_(index<=buffer.size());}
    InputData(){invariant();}
    InputData(InputData&& o):buffer(move((array<byte>&)o.buffer)),index(o.index){invariant();}
    /// Creates a InputData interface to an \a array
    InputData(array<byte>&& array) : buffer(move(array)) {invariant();}
    /// Creates a InputData interface to a \a reference
    explicit InputData(const ref<byte>& reference) : buffer(reference.data,reference.size) {invariant();} //TODO: escape analysis

    /// Returns number of bytes available, reading \a need bytes from underlying device if possible
    virtual uint available(uint /*need*/) { invariant(); return buffer.size()-index; }

    /// Returns next \a size bytes from stream
    ref<byte> get(uint size) const { assert_(index+size<=buffer.size());  return ref<byte>(buffer.data()+index,size); } //TODO: escape analysis
    /// Advances \a count bytes in stream
    void advance(uint count) { invariant(); index+=count; invariant(); }
    /// Returns the next byte in stream without advancing
    byte peek() const { assert_(index<buffer.size()); return buffer[index]; }
    /// Returns the next byte in stream and advance
    byte next() { assert_(index<buffer.size()); byte b=buffer[index]; advance(1); return b; }
    /// Reads \a size bytes from stream
    ref<byte> read(uint size) { ref<byte> t = get(size); advance(size); return t; }
    /// Slices an array referencing this data (valid as long as this stream)
    ref<byte> slice(uint pos, uint size) { return buffer.slice(pos,size); }
    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }
};

#define big32 __builtin_bswap32
inline uint16 __builtin_bswap16(uint16 x) { return (x<<8)|(x>>8); }
#define big16 __builtin_bswap16

/// \a BinaryData provides a convenient interface to parse binary inputs
struct BinaryData : virtual InputData {
    bool isBigEndian = false;

    BinaryData():InputData(){}
    BinaryData(BinaryData&& o):InputData(move(o)),isBigEndian(o.isBigEndian){}
    /// Creates a BinaryData interface to an \a array
    BinaryData(array<byte>&& array, bool isBigEndian=false) : InputData(move(array)), isBigEndian(isBigEndian) {}
    /// Creates a BinaryData interface to a \a reference
    explicit BinaryData(const ref<byte>& reference, bool isBigEndian=false):InputData(reference),isBigEndian(isBigEndian){}
    BinaryData& operator=(BinaryData&& o){(array<byte>&)buffer=move((array<byte>&)o.buffer);index=o.index;isBigEndian=o.isBigEndian;return *this;}

    /// Slices a stream referencing this data
    BinaryData slice(uint pos, uint size) { return BinaryData(array<byte>(buffer.data()+pos,size),isBigEndian); } //TODO: escape analysis
    /// Seeks stream to /a index
    void seek(uint index) { assert_(index<buffer.size()); this->index=index; }
    /// Seeks last match for \a key.
    bool seekLast(const ref<byte>& key);

    /// Reads from stream until next null byte
    ref<byte> untilNull();

    /// Reads one raw \a T element from stream
    template<class T> const T& read() { const T& t = raw<T>(InputData::read(sizeof(T))); return t; }
    int32 read32() { return isBigEndian?big32(read<int32>()):read<int32>(); }
    int16 read16() { return isBigEndian?big16(read<int16>()):read<int16>(); }

    /// Provides template overloaded specialization (for swap) and return type overloading through cast operators.
    struct ReadOperator {
        BinaryData * s;
        /// Reads an int32 and if necessary, swaps from stream to host byte order
        operator uint32() { return s->read32(); }
        operator int32() { return s->read32(); }
        /// Reads an int16 and if necessary, swaps from stream to host byte order
        operator uint16() { return s->read16(); }
        operator int16() { return s->read16(); }
        /// Reads an int8
        operator uint8() { return s->read<uint8>(); }
        operator int8() { return s->read<int8>(); }
        operator byte() { return s->read<byte>(); }
    };
    ReadOperator read() { return __(this); }

    /// Reads \a size raw \a T elements from stream
    template<class T>  ref<T> read(uint size) { return cast<T>(InputData::read(size*sizeof(T))); }

    /// Provides return type overloading for reading arrays (swap as needed)
    struct ArrayReadOperator {
       BinaryData* s; uint size;
       template<class T> operator array<T>() { array<T> t; for(uint i=0;i<size;i++) t<<(T)s->read(); return t; }
   };
   ArrayReadOperator read(uint size) { return __(this,size); }

   /// Reads \a size \a T elements from stream (swap as needed)
   template<class T>  void read(T buffer[], uint size) { for(uint i=0;i<size;i++) buffer[i]=(T)read(); }
};

/// \a TextData provides a convenient interface to parse text streams
struct TextData : virtual InputData {
    TextData():InputData(){}
    TextData(TextData&& o):InputData(move(o)){}
    /// Creates a TextData interface to an \a array
    TextData(array<byte>&& array) : InputData(move(array)){}
    /// Creates a TextData interface to a \a reference
    explicit TextData(const ref<byte>& reference):InputData(reference){}
    //TextData& operator=(TextData&& o){buffer=move(o.buffer);index=o.index;return *this;}

    /// If stream match \a key, advances \a pos by \a key size
    bool match(char key);
    /// If stream match \a key, advances \a pos by \a key size
    bool match(const ref<byte>& key);
    /// If stream match any of \a key, advances \a pos
    bool matchAny(const ref<byte>& any);
    /// If stream match none of \a key, advances \a pos
    bool matchNo(const ref<byte>& any);

    /// advances \a pos while stream doesn't match \a key
    /// \sa until
    ref<byte> whileNot(char key);
    /// advances \a pos while stream match any of \a key
    ref<byte> whileAny(const ref<byte>& key);
    /// advances \a pos while stream match none of \a key
    ref<byte> whileNo(const ref<byte>& key);

    /// Reads until stream match \a key
    /// \sa whileNot
    ref<byte> until(char key);
    /// Reads until stream match \a key
    ref<byte> until(const ref<byte>& key);
    /// Reads until stream match any character of \a key
    ref<byte> untilAny(const ref<byte>& any);
    /// Reads until the end of stream
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
