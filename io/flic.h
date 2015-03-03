#pragma once
#include "bit.h"
#include "image.h"
#include "time.h"
static constexpr size_t EG = 10;
#if RLE
static constexpr size_t RLE = 6;
#endif

struct FLIC : ref<byte> {
    BitReader bitIO;
    //int2 size = 0;
    int predictor = 0;
    size_t valueCount = 0;
    Time decode;
    FLIC() {}
    FLIC(ref<byte> data) : ref<byte>(data), bitIO(data) {
	/*size.x = bitIO.read(32);
	size.y = bitIO.read(32);*/
    }
    //~FLIC() { assert_(valueCount == 0 || valueCount == (size_t)size.y*size.x, valueCount, (size_t)size.y*size.x); }
#if RLE
    size_t rleIndex = 1<<RLE;
#endif
    void read(mref<uint16> buffer) {
	valueCount += buffer.size;
	//assert_(valueCount <= (size_t)size.y*size.x);
	size_t index = 0;
#if RLE
	// Resumes any pending run length
	while(rleIndex < (1<<RLE/*64*/)) {
	    buffer[index] = predictor;
	    index++; rleIndex++;
	    if(index>=buffer.size) return;
	}
#endif
	for(;;) {
	    uint x = bitIO.readExpGolomb<EG>();
#if RLE
	    if(x == 0) {
		rleIndex = 0;
		while(rleIndex < (1<<RLE/*64*/)) {
		    buffer[index] = predictor;
		    index++; rleIndex++;
		    if(index>=buffer.size) return;
		}
	    } else  {
		uint u = x-1;
#else
	    {
		uint u = x;
#endif
		int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
		predictor += s;
		buffer[index] = predictor;
		index++;
		if(index>=buffer.size) return;
	    }
	}
    }
    //~FLIC() { log(size, int(round(Ref::size/(1024.*1024))),"MB", decode, int(round((Ref::size/(1024.*1024))/decode.toReal())), "MB/s"); } // 100 MB/s
};


struct Encoder {
    buffer<byte> buffer;

    BitWriter bitIO {buffer}; // Assumes buffer capacity will be large enough
    int predictor = 0;
    size_t runLength = 0;

    int2 size = 0;
    size_t valueCount = 0;

    Time encode;
    Encoder() {}
    Encoder(int2 size) : buffer((size_t)size.y*size.x*4), size(size) { // Assumes average bitrate < 16bit / sample (large RLE voids)
	//bitIO.write(32, size.x); bitIO.write(32, size.y);
    }
    Encoder& write(ref<uint16> source) {
	valueCount += source.size;
	assert_(valueCount <= (size_t)size.y*size.x, valueCount, size.y, size.x);
	for(size_t index=0;;) {
	    int value, s;
	    for(;;) {
		if(index >= source.size) return *this;
		value = source[index];
		index++;
		s = value - predictor;
#if RLE
		if(s != 0) break;
		runLength++;
	    }
	    flushRLE();
#else
		break;
	    };
#endif
	    predictor = value;
	    uint u  = (s<<1) ^ (s>>31); // s>0 ? (s<<1) : (-s<<1) - 1;
#if RLE
	    uint x = u+1; // 0 is RLE escape
#else
	    uint x = u;
#endif
	    // Exp Golomb _ r
	    x += (1<<EG);
	    uint b = 63 - __builtin_clzl(x); // BSR
	    bitIO.write(b+b+1-EG, x); // unary b, binary q[b], binary r = 0^b.1.q[b]R[r] = x[b+b+1+r]
	}
	return *this;
    }
    /*void flushRLE() {
		if(!runLength) return;
		size_t rleCodeRepeats = runLength >> RLE; // RLE length = rleCodeRepeats · 2^RLE
		for(;rleCodeRepeats > 0; rleCodeRepeats--) bitIO.write(3, 0b100);
		size_t remainingZeroes = runLength & ((1<<RLE)-1);
		for(;remainingZeroes > 0; remainingZeroes--) bitIO.write(3, 0b101); // Explicitly encode remaining zeroes
		runLength = 0;
	}*/

    ::buffer<byte> end() {
	assert_(valueCount == (size_t)size.y*size.x, valueCount, size);
#if RLE
	flushRLE();
#endif
	bitIO.flush();
	buffer.size = (byte*)bitIO.pointer - buffer.data;
	buffer.size += 14; // Pads to avoid segfault with "optimized" BitReader (whose last refill may stride end) // FIXME: 7 bytes should be enough
	assert_(buffer.size <= buffer.capacity);
	//log(int(round(buffer.size/(1024.*1024))),"MB", encode, int(round((buffer.size/(1024.*1024))/encode.toReal())),"MB/s"); // 200 MB/s
	return move(buffer);
    }
};
