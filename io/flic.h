#pragma once
#include "bit.h"
#include "image.h"
#include "time.h"
static constexpr uint EG = 2, RLE = 6;

struct FLIC : ref<byte> {
	BitReader bitIO;
	int2 size;
	int predictor = 0;
	Time decode;
	FLIC(ref<byte> encoded) : ref<byte>(encoded), bitIO(encoded) {
		size.x = bitIO.read(32);
		size.y = bitIO.read(32);
	}
	uint rleIndex = 1<<RLE;
	void read(mref<int16> line) {
		size_t index = 0;
		// Resumes any pending run length
		while(rleIndex < (1<<RLE/*64*/)) {
			line[index] = predictor;
			index++; rleIndex++;
			if(index>=line.size) return;
		}
		for(;;) {
			uint x = bitIO.readExpGolomb<EG>();
			if(x == 0) {
				rleIndex = 0;
				while(rleIndex < (1<<RLE/*64*/)) {
					line[index]=predictor;
					index++; rleIndex++;
					if(index>=line.size) return;
				}
			} else  {
				uint u = x-1;
				int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
				predictor += s;
				line[index] = predictor;
				index++;
				if(index>=line.size) return;
			}
		}
	}
	~FLIC() { log(size, int(round(Ref::size/(1024.*1024))),"MB", decode, int(round((Ref::size/(1024.*1024))/decode.toReal())), "MB/s"); } // 100 MB/s
};
