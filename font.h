#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "data.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

const Folder& fonts();

struct Glyph {
    bool valid=false;
    int2 offset; // (left bearing, min.y-baseline)
    Image image; //not owned
};

/// Freetype wrapper
struct Font {
    Map keep; array<byte> data;
    struct FT_FaceRec_*  face=0;
    float fontSize=0, ascender=0;
    map<uint, map<uint16, Glyph> > cache;

    Font(){}
    move_operator(Font):keep(move(o.keep)),data(move(o.data)),face(o.face),fontSize(o.fontSize),ascender(o.ascender),cache(move(o.cache)){ o.face=0; }
    /// Loads font at /a path scaled to /a size pixels high
    Font(const File& file, int size);
    /// Loads font /a data scaled to /a size pixels high
    Font(array<byte>&& data, int size=0);
    ~Font();
    /// Loads font /a data scaled to /a size pixels high
    void load(const ref<byte>& data, int size);
    /// Sets font size
    void setSize(float size);
    /// Returns font glyph index for glyph \a name
    uint16 index(const ref<byte>& name);
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

    inline int scaleX(int p);
    inline int scaleY(int p);
    inline int scale(int p);
    inline int unscaleX(int p);
    inline int unscaleY(int p);
    inline int unscale(int p);
    inline void line(Bitmap& raster, int2 p0, int2 p1);
    inline void curve(Bitmap& raster, int2 p0, int2 p1, int2 p2);
};
