#pragma once
#include "core.h"
#include "image.h"

struct LJPEG {
 size_t headerSize = 0;
 struct Table {
     uint8 symbolCountsForLength[16];
     int maxLength = 0;
     uint8 symbols[16];
     struct LengthSymbol { uint8 length = 0; uint8 symbol = 0; };
     LengthSymbol lengthSymbolForCode[512];
 } tables[2];
 uint width = 0, height = 0, sampleSize = 0;
 LJPEG(ref<byte> data);
 void decode(const Image16& target, ref<byte> data);
};

size_t encode(const LJPEG& ljpeg, const mref<byte> target, const ref<int16> source);
