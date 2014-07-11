#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "data.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"
#include "thread.h"
struct  FT_FaceRec_;

struct Glyph {
    bool valid=false;
    int2 offset; // (left bearing, min.y-baseline)
    Image image; //not owned
};

/// Freetype wrapper
struct Font {
    /// Loads font /a data scaled to /a size pixels high
    Font(const ref<byte>& data, int size);
    /// Loads font /a data scaled to /a size pixels high
    Font(::buffer<byte>&& buffer, int size=0);
    /// Loads font /a data scaled to /a size pixels high
    Font(Map&& map, int size);

    static Font byName(string name, int size);

    default_move(Font);
    ~Font();

    /// Returns font glyph index for glyph \a name
    uint16 index(const string& name);
    /// Returns font glyph index for Unicode codepoint \a code
    uint16 index(uint16 code);
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

    handle<FT_FaceRec_*> face;
    float fontSize=0, ascender=0;
    map<uint, map<uint16, Glyph>> cache;
    Lock lock;

    buffer<byte> buffer;
    Map map;
};
