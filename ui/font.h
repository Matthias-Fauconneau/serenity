#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "data.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"
struct  FT_FaceRec_;

const Folder& fontFolder();

struct Glyph {
    int2 offset; // (left bearing, min.y-baseline)
    Image image;
};

/// Freetype wrapper
struct Font {
    /// Loads font at /a data to /a size pixels high
    Font(Map&& data, float size);
    /// Loads font /a data scaled to /a size pixels high
    Font(buffer<byte>&& data, float size);
    default_move(Font);
    ~Font();

    /// Returns font glyph index for glyph \a name
    uint index(const string& name);
    /// Returns font glyph index for Unicode codepoint \a code
    uint index(uint code);
    /// Returns hinted advance for \a index
    float advance(uint index);
    /// Returns unhinted advance for \a index
    float linearAdvance(uint index);
    /// Returns size for \a index
    vec2 size(uint index);
    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    float kerning(uint leftIndex, uint rightIndex);
    /// Caches and returns glyph for \a index at position \a x
    /// \a x fractional part is used to return subpixel positionned images
    const Glyph& glyph(uint index);
    /// Renders glyph \a index with transformation matrix \a xx, xy, yx, yy, dx, dy into \a raster
    void render(struct Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy);

    handle<FT_FaceRec_*> face;
    float ascender=0, descender=0;
    map<uint, Glyph> cache;
    buffer<byte> data;
    Map keep;
};
