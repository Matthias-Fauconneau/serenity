#pragma once
#include "data.h"
#include "image.h"

/// 2D array of 16bit integer samples
typedef ImageT<int16> Image16;

struct CR2 {
 bool onlyParse = false;
 ref<byte> data;
 int2 size = 0; size_t stride = 0;
 const uint8* pointer = 0;
 uint bitbuf = 0;
 int vbits = 0;
 uint readBits(const int nbits);

 struct LengthSymbol { uint8 length = 0; uint8 symbol = 0; };
 int maxLength[2] = {0,0};
 buffer<LengthSymbol> lengthSymbolForCode[2] = {};
 int readHuffman(uint i);

 struct { uint16 R, G, B; } whiteBalance = {0,0,0};
 Image16 image;
 size_t huffmanSize = 0;

 void readIFD(BinaryData& s);
 CR2(const ref<byte> file, bool onlyParse=false);
};
