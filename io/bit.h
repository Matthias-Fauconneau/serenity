#pragma once
#include "core.h"

/// Decodes packed bitstreams
struct BitReader : ref<byte> {
	size_t bitSize = 0;
	size_t index = 0;
	BitReader() {}
	BitReader(ref<byte> data) : ref<byte>(data) { bitSize=8*data.size; index=0; }
	/// Reads one lsb bit
	uint bit() {
		uint8 bit = uint8(uint8(data[index/8])<<(7-(index&7))) >> 7;
		index++;
		return bit;
	}
	/// Reads \a size bits
	uint binary(uint size) {
		uint value = (*(uint64*)(data+index/8) << (64-size-(index&7))) >> /*int8*/(64-size);
		index += size;
		return value;
	}
};
