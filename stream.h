#pragma once
#include "array.h"
#include "string.h"
#include "debug.h"

/// References raw memory representation of \a t
template<class T> inline array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }
/// Casts raw memory to \a T
template<class T> inline const T& raw(const array<byte>& a) { assert(a.size()==sizeof(T)); return *(T*)a.data(); }
/// Casts between array element types
template<class T, class O> array<T> cast(array<O>&& o) {
    assert(o.tag==-1); assert((o.size()*sizeof(O))%sizeof(T) == 0);
    return array<T>((const T*)o.buffer.data,o.buffer.size*sizeof(O)/sizeof(T));
}

/// \a Stream is an interface for readers (e.g \a DataStream, \a TextStream) implemented by sources (e.g \a Socket)
struct Stream {
    array<byte> buffer;
    uint index=0;

    Stream(){}
    Stream(Stream&& o):buffer(move(o.buffer)),index(o.index){}
    Stream(array<byte>&& array) : buffer(move(array)) {}
//interface (default to buffer source)
    /// Returns number of bytes available, reading \a need bytes from underlying device if possible
    virtual uint available(uint /*need*/) { return buffer.size()-index; }
    /// Returns next \a size bytes from stream
    virtual array<byte> get(uint size) { assert(index+size<=buffer.size(),index,size,buffer.size());  return array<byte>(buffer.data()+index,size); }
    /// Advances \a count bytes in stream
    virtual void advance(int count) { index+=count;  assert(index<=buffer.size()); }
//Stream helpers
    /// Returns the next byte in stream without advancing
    ubyte peek() const { assert(index<buffer.size()); return buffer[index]; }
    /// Reads \a size bytes from stream
    array<byte> read(uint size) { array<byte> t = get(size); advance(size); return t; }
    /// Returns true if there is data to read
    explicit operator bool() { return available(1); }
};

#define swap32 __builtin_bswap32
inline uint16 __builtin_bswap16(uint16 x) { return (x<<8)|(x>>8); }
#define swap16 __builtin_bswap16

/// \a DataStream provides a convenient interface to parse binaries
struct DataStream : virtual Stream {
    bool bigEndian = false;

    DataStream():Stream(){}
    DataStream(DataStream&& o):Stream(move(o)),bigEndian(o.bigEndian){}
    DataStream(array<byte>&& array, bool bigEndian=false) : Stream(move(array)), bigEndian(bigEndian) {}
    DataStream& operator=(DataStream&& o){buffer=move(o.buffer);index=o.index;bigEndian=o.bigEndian;return *this;}

    /// Slices a stream referencing this data (valid as long as this stream)
    DataStream slice(int pos, int size) { return DataStream(array<byte>(buffer.data()+pos,size),bigEndian); }
    /// Seeks stream to /a index
    void seek(uint index) { assert(index<buffer.size(),index,buffer.size()); this->index=index; }
    /// Seeks last match for \a key.
    bool seekLast(const array<byte>& key) {
        get(-1); //try to completely read source
        for(index=buffer.size()-key.size();index>0;index--) { if(get(key.size()) == key) return true; }
        return false;
    }

    /// Reads from stream until next null byte
    array<byte> untilNull() { array<byte> s(buffer.data()+index,(uint)0); while(peek()) { s.buffer.size++; advance(1); } advance(1); return s; }

    /// Reads one raw \a T element from stream
    template<class T> const T& read() { const T& t = raw<T>(Stream::read(sizeof(T))); return t; }

    /// Reads \a size raw \a T elements from stream
    template<class T>  array<T> read(uint size) { return cast<T>(Stream::read(size*sizeof(T))); }

    /// Provides template overloaded specialization (for swap) and return type overloading through cast operators.
    struct ReadOperator {
        DataStream * s;
        // Swap here and not in read<T>() since functions that differ only in their return type cannot be overloaded
        /// Reads an int32 and if necessary, swaps from stream to host byte order
        operator uint32() { return s->bigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
        operator int32() { return s->bigEndian?swap32(s->read<int32>()):s->read<int32>(); }
        /// Reads an int16 and if necessary, swaps from stream to host byte order
        operator uint16() { return s->bigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
        operator int16() { return s->bigEndian?swap16(s->read<int16>()):s->read<int16>(); }
        /// Reads an int8
        operator uint8() { return s->read<uint8>(); }
        operator int8() { return s->read<int8>(); }
        template<class T> operator const T&(){ return s->read<T>(); }
    };
    ReadOperator read() { return i({this}); }

    /// Provides return type overloading for reading arrays (swap as needed)
    struct ArrayReadOperator {
       DataStream* s; uint size;
       template<class T> operator array<T>() { array<T> t; for(uint i=0;i<size;i++) t<<(T)s->read(); return t; }
   };
   ArrayReadOperator read(uint size) { return i({this,size}); }
};

/// \a TextStream provides a convenient interface to parse texts
struct TextStream : virtual Stream {
    TextStream():Stream(){}
    TextStream(TextStream&& o):Stream(move(o)){}
    TextStream(array<byte>&& array) : Stream(move(array)){}
    TextStream& operator=(TextStream&& o){buffer=move(o.buffer);index=o.index;return *this;}

    /// If stream match \a key, advances \a pos by \a key size
    bool match(const string& key);
    /// If stream match any of \a key, advances \a pos
    bool matchAny(const array<byte>& any);
    /// advances \a pos while stream match any of \a key
    void whileAny(const array<byte>& any);
    /// Reads until stream match \a key
    array<byte> until(const array<byte>& key);
    /// Reads until stream match any character of \a key
    array<byte> untilAny(const array<byte>& any);
    /// Reads until the end of stream
    string untilEnd();
    /// Skips whitespaces
    void skip();
    /// Reads a single word
    string word();
    /// Reads a single XML identifier
    string xmlIdentifier();
    /// Reads a single number
    int number(int base=10);
};
