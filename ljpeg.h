#pragma once
#include "core.h"

struct LJPEG {
 size_t headerSize = 0;
 uint8 symbolCountsForLength[2][16];
 int maxLength[2] = {0,0};
 uint8 symbols[2][16];
 struct LengthSymbol { uint8 length = 0; uint8 symbol = 0; };
 LengthSymbol lengthSymbolForCode[2][512];
 uint width = 01, height = 0, sampleSize = 0;
 void parse(ref<byte> data);
 void decode(const mref<int16> target, ref<byte> data);
};

size_t encode(const LJPEG& ljpeg, const mref<byte> target, const ref<int16> source);
