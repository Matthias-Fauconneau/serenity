#pragma once
#include "array.h"
#include "string.h"
#include "vector.h"

#pragma GCC optimize(3)

/// Stream reader for encoded packed bits
struct BitReader : array<byte> {
    ubyte* data;
    uint bsize;
    uint index;
    BitReader(array<byte>&& buffer);
    /// Skip \a count bytes in stream
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
};

struct FLAC : BitReader {
    const int sampleRate = 48000;
    const int channels = 2;
    const int bitsPerSample = 24;
    uint time;
    int2* buffer=0;
    int blockSize=0;
    int position=0;
    FLAC(array<byte>&& buffer);
    /// Decode next FLAC frame
    void readFrame();
};

extern uint64 rice, predict, order;
