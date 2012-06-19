#pragma once
#include "stream.h"
#include "vector.h"
#include "image.h"
#include "file.h"
#include "map.h"

struct FontMetrics {
    float descender;
    float ascender;
    float height;
};

struct GlyphMetrics {
    int2 advance;
    int2 size;
};

struct Glyph {
    int2 offset;
    int2 advance;
    Pixmap pixmap;
};

struct Font {
    string name;
    Map keep;
    DataStream cmap;
    typedef map<int, Glyph> GlyphCache;
    map<int, GlyphCache> cache;

    Font(string path);
    uint16 index(uint16 code);
    FontMetrics metrics(int size);
    float kerning(int leftCode, int rightCode);
    GlyphMetrics metrics(int size, int code);
    Glyph& glyph(int size, int code);
};
