#pragma once
/// \file flac.h High performance FLAC decoder
#include "memory.h"

/// Simple writable fixed-capacity memory reference
template<class T> struct Buffer {
    T* data=0;
    uint capacity=0,size=0;
    Buffer(){}
    Buffer(uint capacity, uint size=0):data(allocate<T>(capacity)),capacity(capacity),size(size){}
    Buffer(const Buffer& o):Buffer(o.capacity,o.size){copy(data,o.data,size*sizeof(T));}
    move_operator_(Buffer):data(o.data),capacity(o.capacity),size(o.size){o.data=0;}
    ~Buffer(){if(data){unallocate(data);}}
    operator T*() { return data; }
    operator ref<T>() const { return ref<T>(data,size); }
    constexpr const T* begin() const { return data; }
    constexpr const T* end() const { return data+size; }
};

/// Decodes packed bitstreams
struct BitReader {
    const byte* data=0;
    uint bsize=0;
    uint index=0;
    BitReader(){}
    BitReader(const ref<byte>& buffer){setData(buffer);}
    /// Skip \a count bits
    void skip(int count);
    /// Reads one bit in MSB msb encoding
    uint bit();
    /// Reads \a size bits in MSB msb encoding
    uint binary(int size);
    /// Reads \a size bits in MSB msb encoding and two complement sign extend
    int sbinary(int size);
    /// Reads an unary encoded value
    uint unary();
    /// Reads a byte-aligned UTF-8 encoded value
    uint utf8();

    void setData(const ref<byte>& buffer);
};

typedef float float2 __attribute((vector_size(8)));
struct FLAC : BitReader {
    Buffer<float2> buffer __(1<<16);
    uint writeIndex = 0;
    uint readIndex = 0;
    uint16 blockSize = 0, rate = 0;
    uint16 channelMode = 0, sampleSize = 0;
    uint position = 0, duration = 0;

    FLAC(){}
    /// Reads header and decode first frame from data
    FLAC(const ref<byte>& data);
    /// Parses frame header to get next block size (called automatically)
    void parseFrame();
    /// Decodes next FLAC frame
    void decodeFrame();
    /// Reads \a size samples synchronously buffering new frames as needed
    uint read(float2* out, uint size);
};
