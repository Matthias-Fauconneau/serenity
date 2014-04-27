#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "data.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"
struct  FT_FaceRec_;

const Folder& fonts();

struct Glyph {
    bool valid=false;
    int2 offset; // (left bearing, min.y-baseline)
    Image image; //not owned
};

/// Freetype wrapper
struct Font {
    /// Loads font at /a path scaled to /a size pixels high
    Font(const File& file, int size);
    /// Loads font /a data scaled to /a size pixels high
    Font(array<byte>&& data, int size=0);
    default_move(Font);
    ~Font();
    /// Loads font /a data scaled to /a size pixels high
    void load(const ref<byte>& data, int size);
    /// Sets font size
    void setSize(float size);
    /// Returns font glyph index for glyph \a name
    uint16 index(const string& name);
    /// Returns font glyph index for Unicode codepoint \a code
    uint16 index(uint code);
    /// Returns hinted advance for \a index
    float advance(uint16 index);
    /// Returns unhinted advance for \a index
    float linearAdvance(uint16 index);
    /// Returns size for \a index
    vec2 size(uint16 index);
    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    float kerning(uint16 leftIndex, uint16 rightIndex);
    /// Caches and returns glyph for \a index at position \a x
    /// \a x fractional part is used to return subpixel positionned images
    const Glyph& glyph(uint16 index, int x=0);
    /// Renders glyph \a index with transformation matrix \a xx, xy, yx, yy, dx, dy into \a raster
    void render(struct Bitmap& raster, int index, int& xMin, int& xMax, int& yMin, int& yMax, int xx, int xy, int yx, int yy, int dx, int dy);

    Map keep; array<byte> data;
    handle<FT_FaceRec_*> face;
    float fontSize=0, ascender=0;
    map<uint, map<uint16, Glyph>> cache;
};
