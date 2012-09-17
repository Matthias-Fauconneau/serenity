#pragma once
#include "memory.h"

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
    /// Reads an UCS-2 encoded value
    uint utf8();

    void setData(const ref<byte>& buffer);
};

template<class T> struct Buffer {
    T* data=0;
    uint capacity=0,size=0;
    Buffer(uint capacity, uint size=0):data(allocate<T>(capacity)),capacity(capacity),size(size){}
    Buffer(const Buffer& o):Buffer(o.capacity,o.size){copy16(data,o.data,size*sizeof(T)/16);}
    Buffer(Buffer&& o):data(o.data),capacity(o.capacity),size(o.size){o.data=0;}
    move_operator(Buffer)
    ~Buffer(){if(data){unallocate(data,capacity);}}
    operator T*() { return data; }
};

typedef float float2 __attribute((vector_size(8)));
struct FLAC : BitReader {
    Buffer<float2> buffer __(1<<16);
    uint writeIndex = 0;
    uint readIndex = 0;
    uint16 blockSize = 0, rate = 0;
    uint16 channels = 0, sampleSize = 0;
    uint duration = 0;

    default(FLAC)
    /// Reads header and decode first frame from data
    FLAC(const ref<byte>& data);
    /// Parses frame header to get next block size (called automatically)
    void parseFrame();
    /// Decodes next FLAC frame
    void decodeFrame();
};
