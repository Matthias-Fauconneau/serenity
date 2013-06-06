#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "data.h"
#include "string.h"

inline bool isTiff(const ref<byte>& file) { return startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_); }

struct Tiff16 {
    Tiff16(const ref<byte>& file);
    void read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride);
    ~Tiff16();

    BinaryData s;
    struct tiff* tiff;
    uint width, height;
};
