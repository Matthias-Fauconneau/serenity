#pragma once
#include "core.h"

/// Encodes packed bitstreams (msb)
struct BitWriter {
	uint8* pointer;
	uint64 word = 0;
	uint64 bitLeftCount = 64;
	BitWriter(mref<byte> buffer) : pointer((uint8*)buffer.data) {}
	void write(uint size, uint64 value) {
		if(size < bitLeftCount) {
			word <<= size;
			word |= value;
		} else {
			word <<= bitLeftCount;
			word |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
			*(uint64*)pointer = __builtin_bswap64(word); // MSB
			pointer += 8;
			bitLeftCount += 64;
			word = value; // Already stored leftmost bits will be pushed out eventually
		}
		bitLeftCount -= size;
	}
	void end() {
		if(bitLeftCount<64) word <<= bitLeftCount;
		while(bitLeftCount<64) { *pointer++ = word>>(64-8); word <<= 8; bitLeftCount += 8; }
	}
};

/// Decodes packed bitstreams (msb)
struct BitReader {
	uint8* pointer;
	uint64 word;
	int64 bitLeftCount = 64;
	BitReader(ref<byte> data) : pointer((uint8*)data.data) { word=__builtin_bswap64(*(uint64*)pointer); pointer+=8; }
	uint read(uint size) {
		if(bitLeftCount < size/*~12*/) refill(); // conservative. TODO: fit to largest code to refill less often
		uint x = word >> (64-size);
		word <<= size;
		bitLeftCount -= size;
		return x;
	}
	void refill() {
		uint64 next = __builtin_bswap64(*(uint64*)pointer);
		int64 byteCount = (64-bitLeftCount)>>3;
		pointer += byteCount;
		word |= next >> bitLeftCount;
		bitLeftCount += byteCount<<3;
	}

	void align() { bitLeftCount = bitLeftCount/8*8; }
	ref<byte> readBytes(uint byteCount) {
		assert(bitLeftCount%8 == 0);
		pointer -= bitLeftCount/8;
		bitLeftCount = 0;
		ref<byte> slice = ref<byte>((byte*)pointer, byteCount);
		pointer += byteCount;
		return slice;
	}
	template<uint r, uint z=10> uint readExpGolomb() {
		if(bitLeftCount <= z) refill();
		uint b = __builtin_clzl(word) * 2 + 1 + r; // 'word' is kept left aligned, bitLeftCount should be larger than maximum exp golomb zero run length (b)
		return read(b) - (1<<r);
	}
};
