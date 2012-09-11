#pragma once
#include "array.h"

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

typedef float float2 __attribute((vector_size(8)));
struct FLAC : BitReader {
    static constexpr uint channels = 2;
    uint rate = 0;
    uint duration = 0;

    struct Buffer {
        float2* buffer = 0;
        uint capacity = 0;
        Buffer(){}
        Buffer(uint capacity):buffer(allocate<float2>(capacity)),capacity(capacity){}
        Buffer(Buffer&& o):buffer(o.buffer),capacity(o.capacity){o.buffer=0;}
        move_operator(Buffer)
        ~Buffer(){if(buffer)unallocate<float2>(buffer,capacity);}
        operator float2*(){return buffer;}
    } buffer;

    uint blockSize = 0;
    float2* blockIndex = 0; // pointer to start of unread block data (may be modified by consumer)
    float2* blockEnd = 0; // pointer to end of unread block data (call readFrame to read next block)

    FLAC(){}
    /// Reads header and decode first frame to buffer
    FLAC(const ref<byte>& buffer);
    /// Decodes next FLAC block
    void readFrame();
};

extern uint64 rice, predict, order;
