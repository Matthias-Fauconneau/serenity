#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

struct Glyph {
    int2 offset; // (left bearing, min.y-baseline) (in .4)
    int advance=0; //in .4
    Image<uint8> image; //not owned
    Glyph(){}
    Glyph(Glyph&& o)____(=default);
    Glyph(const Glyph& o):offset(o.offset),advance(o.advance),image(share(o.image)){}
};

/// Truetype font renderer stub
struct Font {
    Map keep;
    DataStream cmap;
    uint16* hmtx;
    void* loca; uint16 indexToLocFormat, ascent;
    byte* glyf; uint scale, round, size;
    Glyph cacheASCII[256];
    map<uint16, Glyph> cacheUnicode;

    /// Opens font at /a path scaled to /a size pixels high
    Font(const ref<byte>& path, int size);
    /// Returns kerning space between \a left and \a right
    int kerning(uint16 left, uint16 right); //space in .4
    /// Caches and returns glyph for \a code
    Glyph glyph(uint16 code);
private:
    uint16 index(uint16 code);
    void render(Image<int8>& raster, int index, int16& xMin, int16& xMax, int16& yMin, int16& yMax, int xx, int xy, int yx, int yy, int dx, int dy);
};
