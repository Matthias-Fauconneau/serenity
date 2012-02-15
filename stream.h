#pragma once
#include "array.h"

/// create an array<byte> reference of a types's raw memory representation
template<class T> array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }

constexpr uint32 swap32(uint32 x) { return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24); }
constexpr uint16 swap16(uint16 x) { return ((x>>8)&0xff)|((x&0xff)<<8); }

/// \a Stream provides a convenient interface for reading binary data or text
enum Endianness { LittleEndian,BigEndian };
template<Endianness endianness=LittleEndian> struct Stream {
    const byte* data;
    uint size;
    uint pos=0;
    Stream(const array<byte>& data) : data(&data), size(data.size) {}

    /// Returns true if there is data to read
    explicit operator bool() const { return pos<size; }
    /// Peeks at the next \a size \a T elements in stream without moving \a pos
    template<class T=char> array<T> peek(int size) const { return array<T>((T*)(data+pos),size); }
    /// If stream content match \a key, advances \a pos by \a key size
    template<class T> bool match(const array<T>& key) { if(peek(key.size) == key) { pos+=key.size; return true; } else return false; }

    /// Reads one raw \a T element from stream
    template<class T=char> T read() {
        assert(pos+sizeof(T)<=size); T t = *(T*)(data+pos); pos+=sizeof(T); return t;
    }
    /// Reads \a size raw \a T elements from stream
    template<class T=char> array<T> read(int size) {
        assert((pos+size*sizeof(T))<=this->size); T* t = (T*)(data+pos); pos+=size*sizeof(T); return array<T>(t,size);
    }
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    template<class T=char> array<T> readArray() { uint32 size=read(); return read<T>(size); }

    /// Read an array of raw \a T elements from stream until \a end is read
    template<class T=char, class E> array<T> readUntil(E end) {
        uint length=0; for(;length<size-pos;length++) if(*(E*)(data+pos+length*sizeof(T))==end) break;
        return read(length);
    }

    struct ReadOperator {
        Stream * s;
        /// Reads an uint32 and if necessary, swaps from stream to host byte order
        operator uint32() { return endianness==BigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
        /// Reads an uint16 and if necessary, swaps from stream to host byte order
        operator uint16() { return endianness==BigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
        /// Reads a \a T element (deduced from the destination (return type overload))
        template<class T> operator T() { return s->read<T>(); }
    };
    /// Returns an operator to deduce the type to read from the destination (return type overload)
    ReadOperator read() { return ReadOperator{this}; }

    /// Skips a byte from the stream
    Stream& operator ++(int) { pos++; return *this; }
    /// Skips \a count byte from the stream
    Stream& operator +=(int count) { pos+=count; return *this; }
    /// Align the stream position to the next \a width
    template <int width> void align() { pos=::align<width>(pos); }

    array<byte> slice(int offset, int size) const { return array<byte>(data+pos+offset,size); }
    operator array<byte>() { return slice(0,size-pos); }
};
