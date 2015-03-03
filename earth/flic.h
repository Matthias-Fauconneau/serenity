#pragma once
#include "bit.h"
#include "image.h"
#include "time.h"
static constexpr size_t EG = 2, RLE = 6;

struct FLIC : ref<byte> {
	BitReader bitIO;
	int2 size = 0;
	int predictor = 0;
	size_t valueCount = 0;
	Time decode;
	FLIC() {}
	FLIC(ref<byte> data) : ref<byte>(data), bitIO(data) {
		size.x = bitIO.read(32);
		size.y = bitIO.read(32);
	}
	~FLIC() { assert_(valueCount == 0 || valueCount == (size_t)size.y*size.x, valueCount, (size_t)size.y*size.x); }
	size_t rleIndex = 1<<RLE;
	void read(mref<int16> buffer) {
		valueCount += buffer.size;
		assert_(valueCount <= (size_t)size.y*size.x);
		size_t index = 0;
		// Resumes any pending run length
		while(rleIndex < (1<<RLE/*64*/)) {
			buffer[index] = predictor;
			index++; rleIndex++;
			if(index>=buffer.size) return;
		}
		for(;;) {
			uint x = bitIO.readExpGolomb<EG>();
			if(x == 0) {
				rleIndex = 0;
				while(rleIndex < (1<<RLE/*64*/)) {
					buffer[index] = predictor;
					index++; rleIndex++;
					if(index>=buffer.size) return;
				}
			} else  {
				uint u = x-1;
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
