#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

struct Glyph {
    bool valid=false;
    int2 offset; // (left bearing, min.y-baseline) (in .4)
    Image image; //not owned
};

/// Truetype font renderer stub
struct Font {
    Map keep;
    DataStream cmap, kern;
    uint16* hmtx;
    const void* loca; uint16 indexToLocFormat; int ascent, descent, lineGap;
    const byte* glyf; uint scale; int round, size;
    Glyph cacheASCII[16][128];
    map<uint16, Glyph> cacheUnicode[16];

    /// Opens font at /a path scaled to /a size pixels high
    Font(const ref<byte>& path, int size);
    /// Returns font glyph index for Unicode codepoint \a code
    uint16 index(uint16 code);
    /// Returns advance for \a index
    int advance(uint16 index);
    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    int kerning(uint16 leftIndex, uint16 rightIndex); //space in .4
    /// Caches and returns glyph for \a index at position \a x (in .4)
    /// \a x fractional part is used to return subpixel positionned images
    const Glyph& glyph(uint16 index, int x=0);
    /// Renders glyph \a index with transformation matrix \a xx, xy, yx, yy, dx, dy into \a raster
    void render(struct Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy);
};
