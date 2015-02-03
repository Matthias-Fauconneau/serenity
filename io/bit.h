#pragma once
#include "core.h"

/// Encodes packed bitstreams (msb)
struct BitWriter {
	uint8* pointer = 0;
	uint64 word = 0;
	uint64 bitLeftCount = 64;
	BitWriter() {}
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
	template<uint r, uint z=10> uint readExpGolomb() {
		if(bitLeftCount <= z) refill();
		uint b = __builtin_clzl(word) * 2 + 1 + r; // 'word' is kept left aligned, bitLeftCount should be larger than maximum exp golomb zero run length (b)
		return read(b) - (1<<r);
	}
};

/// Decodes packed bitstreams (lsb)
struct BitReaderLSB : ref<byte> {
	size_t index = 0;
	BitReaderLSB(ref<byte> data) : ref<byte>(data) {}
	/// Reads \a size bits
	uint read(uint size) {
		uint value = (*(uint64*)(data+index/8) << (64-size-(index&7))) >> /*int8*/(64-size);
		index += size;
		return value;
	}
	void align() { index = (index + 7) & ~7; }
	ref<byte> readBytes(uint byteCount) {
		assert(index&7 == 0);
		ref<byte> slice = ref<byte>((byte*)data+index/8, byteCount);
		index += byteCount*8;
		return slice;
	}
};
