#pragma once
#include "core.h"

/// Encodes packed bitstreams (msb)
struct BitWriter {
	uint8* pointer = 0;
	uint64 word = 0;
	int64 bitLeftCount = 64;
	uint8* end = 0;
	BitWriter() {}
	BitWriter(mref<byte> buffer) : pointer((uint8*)buffer.begin()), end((uint8*)buffer.end()) {}
	void write(uint size, uint64 value) {
		if(size < bitLeftCount) {
			word <<= size;
			word |= value;
		} else {
			word <<= bitLeftCount;
			word |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
			assert_(pointer < end);
			*(uint64*)pointer = __builtin_bswap64(word); // MSB
			pointer += 8;
			bitLeftCount += 64;
			word = value; // Already stored leftmost bits will be pushed out eventually
		}
		bitLeftCount -= size;
	}
	void flush() {
		if(bitLeftCount<64) word <<= bitLeftCount;
		while(bitLeftCount<64) { assert_(pointer < end); *pointer++ = word>>(64-8); word <<= 8; bitLeftCount += 8; }
	}
};

/// Decodes packed bitstreams (msb)
struct BitReader {
	uint8* pointer = 0;
	uint64 word = 0;
	int64 bitLeftCount = 64;
	uint8* begin = 0;
	uint8* end = 0;
	BitReader() {}
	BitReader(ref<byte> data) : pointer((uint8*)data.begin()), begin(pointer), end((uint8*)data.end()) {
		word = __builtin_bswap64(*(uint64*)pointer);
		pointer += 8;
	}
	~BitReader() { assert_(pointer==begin+8 || pointer+14>=end, bitLeftCount, end-pointer); }
	uint read(uint size) {
		if(bitLeftCount < size/*~12*/) refill(size); // conservative. TODO: fit to largest code to refill less often
		assert_(bitLeftCount >= size);
		uint x = word >> (64-size);
		word <<= size;
		bitLeftCount -= size;
		assert_(bitLeftCount >= 0);
		return x;
	}
	void refill(size_t size) {
		assert_(pointer+8<=end, bitLeftCount, pointer, end, end-pointer);
		uint64 next = __builtin_bswap64(*(uint64*)pointer);
		int64 byteCount = (64-bitLeftCount)>>3;
		pointer += byteCount;
		word |= next >> bitLeftCount;
		bitLeftCount += byteCount<<3;
		assert_(bitLeftCount >= int64(size), bitLeftCount, size);
	}
	template<uint r, size_t z=14> uint readExpGolomb() {
		if(bitLeftCount <= int64(z)) refill(z);
		// 'word' is kept left aligned, bitLeftCount should be larger than maximum exp golomb zero run length
		size_t size = __builtin_clzl(word);
		assert_(size<z, size);
		uint b = size * 2 + 1 + r;
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
		assert((index&7) == 0);
		ref<byte> slice = ref<byte>((byte*)data+index/8, byteCount);
		index += byteCount*8;
		return slice;
	}
};