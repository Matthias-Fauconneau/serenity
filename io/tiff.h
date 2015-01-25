#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "data.h"
#include "string.h"
#include "image.h"

struct Image16 {
    uint width = 0, height = 0, stride = 0;
    const int16* data;
};

Image16 parseTIFF(const ref<byte> file);
