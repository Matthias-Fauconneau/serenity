#pragma once
#include "data.h"
#include "ljpeg.h"
#include "rans4.h"

struct CR2 {
 struct Entry { uint16 tag, type; uint count; uint value; };
 size_t tiffHeaderSize, dataSize;
 rgb3 whiteBalance;
 float focalLengthMM = 0;
 LJPEG ljpeg;
 Image16 image;

 CR2(const ref<byte> file, bool onlyParse=false);
};
