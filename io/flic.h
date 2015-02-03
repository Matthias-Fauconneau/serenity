#pragma once
#include "bit.h"
#include "image.h"
#include "time.h"
static constexpr uint EG = 2, RLE = 6;

Image16 decodeFLIC(ref<byte> encoded) {
	Time decode;
	BitReader bitIO (encoded);
	uint width = bitIO.read(32);
	uint height = bitIO.read(32);
	Image16 image(int2(width, height));
	int predictor = 0;
	for(size_t index=0; index<height*width;) {
		uint x = bitIO.readExpGolomb<EG>();
		if(x == 0) for(uint unused i: range(1<<RLE/*64*/)) { image[index]=predictor; index++; }
		else  {
			uint u = x-1;
			int s = (u>>1) ^ (-(u&1)); // u&1 ? -((u>>1) + 1) : u>>1;
			predictor += s;
			image[index] = predictor;
			index++;
		}
	}
	log(width, height, int(round(encoded.size/(1024.*1024))),"MB", decode, int(round((encoded.size/(1024.*1024))/decode.toReal())),"MB/s"); // 73-107 MB/s
	return image;
}
