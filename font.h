#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

struct Glyph {
    int2 offset; // (left bearing, min.y-baseline) (in .4)
    int advance=0; //in .4
    Image image; //not owned
    Glyph(){}
    Glyph(Glyph&& o)____(=default);
    Glyph(const Glyph& o):offset(o.offset),advance(o.advance),image(share(o.image)){}
};

/// Truetype font renderer stub
struct Font {
    Map keep;
    DataStream cmap, kern;
    uint16* hmtx;
    void* loca; uint16 indexToLocFormat, ascent;
    byte* glyf; int scale, round, size;
    Glyph cacheASCII[16][256];
    map<uint16, Glyph> cacheUnicode[16];

    /// Opens font at /a path scaled to /a size pixels high
    Font(const ref<byte>& path, int size);
    /// Returns font glyph index for Unicode codepoint \a code
    uint16 index(uint16 code);
    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    int kerning(uint16 leftIndex, uint16 rightIndex); //space in .4
    /// Caches and returns glyph for \a index at position \a x (in .4)
    /// \a x fractional part is used to return subpixel positionned images
    Glyph glyph(uint16 index, int x=0);
private:
    void render(struct Bitmap& raster, int index, int16& xMin, int16& xMax, int16& yMin, int16& yMax, int xx, int xy, int yx, int yy, int dx, int dy);
};
