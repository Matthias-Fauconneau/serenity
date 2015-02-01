#pragma once
#include "memory.h"
#include "vector.h"
/// \file flac.h High performance FLAC decoder

/// Decodes packed bitstreams
struct BitReader : ref<uint8> {
	size_t bitSize = 0;
	size_t index = 0;
	BitReader() {}
	BitReader(ref<uint8> data);
	/// Skip \a count bits
	void skip(int count);
	/// Reads one bit
	uint bit();
	/// Reads \a size bits
	uint binary(int size);
	/// Reads \a size bits and two complement sign extend
	int sbinary(int size);
	/// Reads an unary encoded value
	uint unary();
	/// Reads a byte-aligned UTF-8 encoded value
	uint utf8();
};

struct FLAC : BitReader {
	default_move(FLAC);
	static constexpr size_t maxBlockSize = 1<<16;
	buffer<float2> audio {maxBlockSize};
	size_t writeIndex = 0;
	size_t readIndex = 0;
	size_t audioAvailable = 0;
	size_t blockSize = 0;
	uint rate = 0;
	uint channelMode = 0, sampleSize = 0;
	uint position = 0, duration = 0;

	FLAC() {}
	/// Reads header and decode first frame from data
	FLAC(ref<byte> data);
	/// Parses frame header to get next block size (called automatically)
	void parseFrame();
	/// Decodes next FLAC frame
	void decodeFrame();
	/// Reads \a size samples synchronously buffering new frames as needed
	size_t read(mref<float2> out);
};
inline FLAC copy(const FLAC& o) {
	FLAC t ;
	t.data=o.data, t.bitSize=o.bitSize, t.index=o.index; t.audio=copy(o.audio);
	t.writeIndex=o.writeIndex, t.readIndex=o.readIndex; t.audioAvailable = o.audioAvailable;
	t.blockSize=o.blockSize, t.rate=o.rate, t.channelMode=o.channelMode, t.sampleSize=o.sampleSize;
	t.position=o.position, t.duration=o.duration;
	return t;
}
