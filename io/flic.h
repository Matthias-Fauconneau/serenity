#pragma once
#include "bit.h"
static constexpr size_t EG = 6;

struct FLIC : ref<byte> {
    BitReader bitIO;
	int predictor = 0;

    FLIC() {}
	FLIC(ref<byte> data) : ref<byte>(data), bitIO(data) {}

    void read(mref<uint16> buffer) {
	size_t index = 0;
	for(;;) {
		uint u = bitIO.readExpGolomb<EG>();
		int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
		predictor += s;
		buffer[index] = predictor;
		index++;
		if(index>=buffer.size) return;
	}
	}
};

struct Encoder : buffer<byte>{
	BitWriter bitIO {*this}; // Assumes buffer capacity will be large enough
    int predictor = 0;

	Encoder(size_t capacity) : buffer(capacity) {}
	Encoder& write(ref<uint16> source) {
		for(size_t index=0;;) {
			if(index >= source.size) return *this;
			int value = source[index];
			index++;
			int s = value - predictor;
			predictor = value;
			uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
			// Exp Golomb _ r
			uint x = u + (1<<EG);
			uint b = 63 - __builtin_clzl(x); // BSR
			bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
		}
		return *this;
	}
	::buffer<byte> end() {
		bitIO.flush();
		size = (byte*)bitIO.pointer - data;
		size += 7; // Pads to avoid segfault with "optimized" BitReader (whose last refill may stride end) // FIXME: 7 bytes should be enough
		return ::move(*this);
	}
};
