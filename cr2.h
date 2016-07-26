#pragma once
#include "data.h"
#include "image.h"

/// 2D array of 16bit integer samples
typedef ImageT<int16> Image16;

static constexpr uint L = 1u << 16;
static constexpr uint scaleBits = 15; // < 16
static constexpr uint M = 1<<scaleBits;

struct CR2 {
 bool onlyParse = false;
 array<uint*> zeroOffset; // Words to zero to delete JPEG thumb (but keep EXIF) (1MB)
 array<uint*> ifdOffset; // Replace nextIFD after JPEG thumb with 3rd IFD (RAW) to remove RGB thumb (1MB)
 struct Entry { uint16 tag, type; uint count; uint value; };
 array<Entry*> entriesToFix; // Entries which would have dangling references after truncation
 ref<byte> data;
 int2 size = 0; size_t stride = 0;

 const uint8* begin = 0;
 const uint8* pointer = 0;
 const uint8* end = 0;
 uint bitbuf = 0;
 int vbits = 0;
 uint readBits(const int nbits);

 struct LengthSymbol { uint8 length = 0; uint8 symbol = 0; };
 int maxLength[2] = {0,0};
 ref<uint8> symbolCountsForLength[2];
 ref<uint8> symbols[2];
 LengthSymbol lengthSymbolForCode[2][512];
 int readHuffman(uint i);

 struct { uint16 R, G, B; } whiteBalance = {0,0,0};
 Image16 image;

 void readIFD(BinaryData& s);
 CR2(const ref<byte> file, bool onlyParse=false);
};
