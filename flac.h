#pragma once
#include "array.h"
#include "vector.h"

/// Decodes packed bitstreams
struct BitReader : array<byte> {
    ubyte* data=0;
    uint bsize=0;
    uint index=0;
    BitReader(){}
    BitReader(array<byte>&& buffer){setData(move(buffer));}
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

    void setData(array<byte>&& buffer);
};

struct FLAC : BitReader {
    FLAC(){}
    const int sampleRate = 48000;
    const int channels = 2;
    const int bitsPerSample = 24;
    uint time=0;
    int2 buffer[8192]; //64K
    int blockSize=0;
    int position=0;
    /// Read header and prepare to read frames
    void open(array<byte>&& data);
    /// Decode next FLAC frame
    void readFrame();
};

extern uint64 rice, predict, order;
