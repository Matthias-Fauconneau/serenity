#pragma once
#include "array.h"
#include "vector.h"

/// Decodes packed bitstreams
struct BitReader {
    const byte* data=0;
    uint bsize=0;
    uint index=0;
    BitReader(){}
    BitReader(const ref<byte>& buffer){setData(buffer);}
    /// Skip \a count bits in stream
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

struct FLAC : BitReader {
    uint maxBlockSize = 0;
    uint rate = 0;
    const uint channels = 2;
    uint sampleSize = 0;
    uint duration = 0;

    float* buffer = 0;
    uint blockSize=0;
    uint frame=0;

    no_copy(FLAC)
    FLAC(){}
    FLAC(FLAC&& o){copy((byte*)this,(byte*)&o,sizeof(FLAC)); o.buffer=0;}
    ~FLAC(){if(buffer) unallocate<float>(buffer,channels*maxBlockSize);}

    /// Reads header and prepare to read frames
    void start(const ref<byte>& buffer);
    /// Decodes next FLAC block
    void readFrame();
};

extern uint64 rice, predict, order;
