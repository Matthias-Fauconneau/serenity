#pragma once
#include "vector.h"
#include "image.h"
#include "map.h"

struct FontMetrics {
    float descender;
    float ascender;
    float height;
};

struct GlyphMetrics {
    vec2 advance;
    vec2 size;
};

struct Glyph {
    vec2 offset;
    vec2 advance;
    Image image;
};

struct FT_FaceRec_;
struct Font {
    string name;
    string data;
    FT_FaceRec_* face=0;
    map<int, map<int, Glyph> > cache;
    array<float> widths;

    Font(){}
    Font(const char* path);
    Font(string&& data);
    FontMetrics metrics(int size);
    float kerning(int leftCode, int rightCode);
    GlyphMetrics metrics(int size, int code);
    Glyph& glyph(int size, int code);
};

extern Font defaultSans;
extern Font defaultBold;
extern Font defaultItalic;
extern Font defaultBoldItalic;
