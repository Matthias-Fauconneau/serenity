#pragma once
#include "core.h"
typedef unsigned long word;
#include "string.h"

/// Encodes packed bitstreams (msb)
struct BitWriter {
	uint8* pointer = 0;
	::word word = 0;
	uint bitLeftCount = sizeof(word)*8;
	uint8* end = 0;
	BitWriter() {}
	BitWriter(mref<byte> buffer) : pointer((uint8*)buffer.begin()), end((uint8*)buffer.end()) {}
	void write(uint size, ::word value) {
		assert_(size <= sizeof(word)*8-8, size);
		if(size < bitLeftCount) {
			word <<= size;
			word |= value;
		} else {
			assert_(bitLeftCount<sizeof(word)*8);
			word <<= bitLeftCount;
			word |= value >> (size - bitLeftCount); // Puts leftmost bits in remaining space
			bitLeftCount += sizeof(word)*8;
			assert_(bitLeftCount >= size, size, bitLeftCount, sizeof(word));
			assert_(pointer < end, pointer, end);
			*(::word*)pointer = (sizeof(word)==4 ? __builtin_bswap32(word) : __builtin_bswap64(word)); // MSB
			word = value; // Already stored leftmost bits will be pushed out eventually
			pointer += sizeof(word);
		}
		bitLeftCount -= size;
	}
	void flush() {
		if(bitLeftCount<sizeof(word)*8) word <<= bitLeftCount;
		while(bitLeftCount<sizeof(word)*8) {
			assert_(pointer < end);
			*pointer++ = word>>(sizeof(word)*8-8);
			word <<= 8;
			bitLeftCount += 8;
		}
	}
};

/// Decodes packed bitstreams (msb)
struct BitReader {
	uint8* pointer = 0;
	::word word = 0;
	uint bitLeftCount = sizeof(word)*8;
	uint8* begin = 0;
	uint8* end = 0;
	BitReader() {}
	BitReader(ref<byte> data) : pointer((uint8*)data.begin()), begin(pointer), end((uint8*)data.end()) {
		word = (sizeof(word)==4 ? __builtin_bswap32(*(uint32*)pointer) : __builtin_bswap64(*(uint64*)pointer));
		pointer += sizeof(word);
	}
	~BitReader() { assert_(pointer==begin+8 || pointer+14>=end, bitLeftCount, end-pointer); }
	uint read(uint size) {
		if(bitLeftCount < size/*~12*/) refill(size); // conservative. TODO: fit to largest code to refill less often
		assert_(bitLeftCount >= size);
		uint x = word >> (sizeof(word)*8-size);
		word <<= size;
		bitLeftCount -= size;
		return x;
	}
	void refill(size_t size) {
		assert_(pointer+sizeof(word)<=end, size, bitLeftCount, pointer, end, end-pointer);
		::word next = (sizeof(word)==4 ? __builtin_bswap32(*(uint32*)pointer) : __builtin_bswap64(*(uint64*)pointer));
		int byteCount = (sizeof(word)*8-bitLeftCount)/8;
		pointer += byteCount;
		assert_(bitLeftCount<sizeof(word)*8);
		word |= next >> bitLeftCount;
		bitLeftCount += byteCount*8;
		assert_(bitLeftCount >= size, bitLeftCount, size);
	}
	template<uint r, uint z=24/*14*/> uint readExpGolomb() {
		if(bitLeftCount <= z) refill(z);
		// 'word' is kept left aligned, bitLeftCount should be larger than maximum exp golomb zero run length
		assert_(word, begin, pointer, end, bitLeftCount);
		uint size = __builtin_clzl(word);
		assert_(size<=z, size, z, (size_t)word);
		uint b = size * 2 + 1 + r;
		return read(b) - (1<<r);
	}
};
