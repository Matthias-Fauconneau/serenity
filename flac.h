#pragma once
/// \file flac.h High performance FLAC decoder
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
