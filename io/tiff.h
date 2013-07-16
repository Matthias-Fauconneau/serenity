#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "data.h"
#include "string.h"
#include "image.h"

inline bool isTiff(const ref<byte>& file) { return startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_); }

struct Tiff16 {
    Tiff16(const ref<byte>& file);
    ~Tiff16();
    void read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride);
    explicit operator bool() { return tiff; }

    BinaryData s;
    struct tiff* tiff = 0;
    uint width = 0, height = 0;
    bool randomAccess = false;
};

/// Wraps image using TIFF headers
buffer<byte> encodeTIFF(const Image16& image);
