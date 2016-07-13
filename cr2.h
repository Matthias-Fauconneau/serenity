#pragma once
#include "data.h"
#include "image.h"

/// 2D array of 16bit integer samples
typedef ImageT<uint16> Image16;

struct CR2 {
 struct { uint16 R, G, B; } whiteBalance = {0,0,0};
 Image16 image;

 const uint8* pointer = 0;
 uint bitbuf = 0;
 int vbits = 0;
 uint readBits(const int nbits);

 struct LengthSymbol { uint8 length; uint8 symbol; };
 int maxLength[2];
 buffer<LengthSymbol> lengthSymbolForCode[2];
 uint8 readHuffman(uint i);

 void readIFD(BinaryData& s);
 CR2(const ref<byte> file);
};
