#pragma once
#include "array.h"

/// create an array<byte> reference of a types's raw memory representation
template<class T> array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }

constexpr uint32 swap32(uint32 x) { return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24); }
constexpr uint16 swap16(uint16 x) { return ((x>>8)&0xff)|((x&0xff)<<8); }

/// \a Stream provides a convenient interface for reading binary data or text
enum Endianness { LittleEndian,BigEndian };
template<Endianness endianness> struct EndianStream : array<byte> {
    const byte* pos,*end;
    EndianStream(array<byte>&& buffer) : array(move(buffer)), pos(data), end(data+size) {}

    /// Returns true if there is data to read
    explicit operator bool() const { return pos<end; }

    /// Peeks at the next raw \a T element in stream without moving \a pos
    template<class T> const T& peek() const { return *(T*)(pos); }
    /// Peeks at the next \a size raw \a T elements in stream without moving \a pos
    template<class T> array<T> peek(int size) const { return array<T>((T*)(pos),size); }


    /// Reads one raw \a T element from stream
    template<class T> T read() {
        assert(pos+sizeof(T)<=end); T t = *(T*)(pos); pos+=sizeof(T); return t;
    }
    /// Reads \a size raw \a T elements from stream
    template<class T> array<T> read(int size) {
        assert((pos+size*sizeof(T))<=end); array<T> t(pos,size); pos+=size*sizeof(T); return t;
    }
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    template<class T> array<T> readArray() { uint32 size=read(); return read<T>(size); }

    struct ReadOperator {
        EndianStream * s;
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
    EndianStream& operator ++(int) { pos++; return *this; }
    /// Skips \a count byte from the stream
    EndianStream& operator +=(int count) { pos+=count; return *this; }
    /// Align the stream position to the next \a width
    template <int width> void align() { pos=::align<width>(pos); }

    /// If stream match any of \a key, advances \a pos
    template<class T> bool matchAny(const array<T>& any) { for(const T& e: any) if(peek<T>() == e) { pos+=sizeof(T); return true; } return false; }

    /// If stream match \a key, advances \a pos by \a key size
    template<class T> bool match(const array<T>& key) { if(peek<T>(key.size) == key) { pos+=key.size*sizeof(T); return true; } else return false; }

    /// advances \a pos until stream match \a key
    /// \returns no match count
    template<class T> array<T> until(const T& key) {
        const byte* start=pos;
        while(pos<end && peek<T>()!=key) pos+=sizeof(T);
        pos+=sizeof(T);
        return array(start,pos-sizeof(T));
    }

    /// advances \a pos while stream match any of \a key
    /// \returns reference to matched data
    template<class T> array<T> whileAny(const array<T>& any) {
        const byte* start=pos;
        while(pos<end && matchAny(any)) {}
        return array(start,pos);
    }

    /// advances \a pos until stream match any of \a key
    /// \returns no match count
    template<class T> array<T> untilAny(const array<T>& any) {
        const byte* start=pos;
        while(pos<end && !matchAny(any)) pos+=sizeof(T);
        return array(start,pos-sizeof(T));
    }


    //array<byte> slice(int offset, int size) const { return array<byte>(data+pos+offset,size); }
    //operator array<byte>() { return slice(0,size-pos); }
};
typedef EndianStream<LittleEndian> Stream; //host byte order
typedef EndianStream<BigEndian> NetworkStream; //network byte order

//TODO: utf8
struct TextStream : Stream {
    TextStream(string&& buffer) : Stream(move(buffer)) {}
    long readInteger(int base=10);
    double readFloat(int base=10);
    //string word() { int start=pos; while(data[pos]!=' '&&data[pos]!='\n'&&data[pos]!='\r') pos++; return string((const char*)data+start,pos-start); }
    //char operator*() { return peek(); }
    //operator string() { return string((const char*)data+pos,size-pos); }
    //operator const char*() { return (const char*)data+pos; }
};
