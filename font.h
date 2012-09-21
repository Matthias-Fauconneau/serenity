#pragma once
#include "data.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

const Folder& fonts();

struct Glyph {
    bool valid=false;
    int2 offset; // (left bearing, min.y-baseline) (in .4)
    Image image; //not owned
};

/// Freetype wrapper
struct Font {
    Map keep; array<byte> data;
    struct FT_FaceRec_*  face=0;
    int nominalSize=0, ascender=0;
    Glyph cacheASCII[128];
    map<uint16, Glyph> cacheUnicode;

    /// Loads font at /a path scaled to /a size pixels high
    Font(const File& file, int size);
    /// Loads font /a data scaled to /a size pixels high
    Font(array<byte>&& data, int size=0);
    /// Loads font /a data scaled to /a size pixels high
    void load(const ref<byte>& data, int size);
    /// Sets font size in .6 pixels
    void setSize(int size);
    /// Returns font glyph index for Unicode codepoint \a code
    uint16 index(uint16 code);
    /// Returns advance for \a index
    int advance(uint16 index);
    /// Returns size for \a index
    vec2 size(uint16 index);
    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    int kerning(uint16 leftIndex, uint16 rightIndex); //space in .4
    /// Caches and returns glyph for \a index at position \a x (in .4)
    /// \a x fractional part is used to return subpixel positionned images
    const Glyph& glyph(uint16 index, int x=0);
    /// Renders glyph \a index with transformation matrix \a xx, xy, yx, yy, dx, dy into \a raster
    void render(struct Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy);

    inline int scaleX(int p);
    inline int scaleY(int p);
    inline int scale(int p);
    inline int unscaleX(int p);
    inline int unscaleY(int p);
    inline int unscale(int p);
    inline void line(Bitmap& raster, int2 p0, int2 p1);
    inline void curve(Bitmap& raster, int2 p0, int2 p1, int2 p2);
};
