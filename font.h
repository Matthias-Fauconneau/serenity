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
    Image<ubyte> image;
};

struct Font {
    string name;

    Map keep;

    DataStream cmap;

    void* loca;
    int16 indexToLocFormat;

    byte* glyf;
    uint16 unitsPerEm;

    typedef map<int, Glyph> GlyphCache;
    map<int, GlyphCache> cache;

    Font(string path);
    int kerning(uint16 leftCode, uint16 rightCode);
    /// Caches and returns glyph for \a code a \a size
    /// \note Glyph references might become dangling on cache resize
    const Glyph& glyph(int size, uint16 code);
private:
    uint16 index(uint16 code);
};
