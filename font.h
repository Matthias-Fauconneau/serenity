#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

// All coordinates are .8 fixed point

struct Glyph {
    int2 offset; // (left bearing, min.x-baseline)
    int2 advance;
    Image<gray> image;
};

/// Truetype font renderer stub
struct Font {
    Map keep;
    DataStream cmap;
    void* loca; int16 indexToLocFormat;
    byte* glyf; int scale, round, size;
    Glyph cache[256]; //TODO: Unicode

    Font(string path, int size);
    /// Returns kerning space between \a left and \a right
    int kerning(uint16 left, uint16 right);
    /// Caches and returns glyph for \a code
    Glyph glyph(uint16 code);
private:
    uint16 index(uint16 code);
};
