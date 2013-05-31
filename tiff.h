#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "data.h"

struct Tiff16 {
    Tiff16(const ref<byte>& file);
    void read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride);
    ~Tiff16();

    BinaryData s;
    struct tiff* tiff;
    uint width, height;
};
