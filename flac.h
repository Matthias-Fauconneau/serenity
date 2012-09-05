#pragma once
#include "array.h"
#include "vector.h"

/// Decodes packed bitstreams
struct BitReader : array<byte> {
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
    FLAC(){}
    const int sampleRate = 48000;
    const int channels = 2;
    const int bitsPerSample = 24;
    uint time=0;
    int2 buffer[8192]; //64K
    int blockSize=0;
    int position=0;
    /// Read header and prepare to read frames
    void start(const ref<byte>& buffer);
    /// Decode next FLAC frame
    void readFrame();
};

extern uint64 rice, predict, order;
