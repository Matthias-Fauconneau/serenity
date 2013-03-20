#pragma once
#include "core.h"
/// \file flac.h High performance FLAC decoder

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
    buffer<float2> audio {1<<16,0};
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
inline FLAC copy(const FLAC& o) { FLAC t; t.data=o.data, t.bsize=o.bsize, t.index=o.index, t.audio=copy(o.audio), t.writeIndex=o.writeIndex, t.readIndex=o.readIndex, t.blockSize=o.blockSize, t.rate=o.rate, t.channelMode=o.channelMode, t.sampleSize=o.sampleSize, t.position=o.position, t.duration=o.duration; return t; }
