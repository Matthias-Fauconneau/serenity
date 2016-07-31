#pragma once
#include "data.h"
#include "ljpeg.h"
#include "rans4.h"

struct CR2 {
 struct Entry { uint16 tag, type; uint count; uint value; };
 size_t tiffHeaderSize, dataSize;
 struct { uint16 R, G, B; } whiteBalance = {0,0,0};
 float focalLengthMM = 0;
 LJPEG ljpeg;
 Image16 image;

 CR2(const ref<byte> file, bool onlyParse=false);
};
