#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "file.h"
#include "image.h"
#include "map.h"
struct  FT_FaceRec_;

const Folder& fontFolder();
String findFont(string fontName, ref<string> fontTypes={""_});

/// Freetype wrapper
struct Font {
    /// Loads font at /a data to /a size pixels high
    Font(Map&& data, float size, bool hint=false, string id=""_);
    /// Loads font /a data scaled to /a size pixels high
    Font(buffer<byte>&& data, float size, bool hint=false, string id=""_);
    default_move(Font);
    ~Font();

    /// Returns font glyph index for Unicode codepoint \a code
    uint index(uint code) const;

    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    float kerning(uint leftIndex, uint rightIndex);

    struct Metrics {
        float advance;
        int leftDelta, rightDelta; // 26.6 bearing delta to correct kerning with hinting
        union {
            struct { float width, height; };
            vec2 size;
        };
    };

    /// Returns hinted advance for \a index
    Metrics metrics(uint index) const;

    struct Glyph {
        int2 offset; // (left bearing, min.y-baseline) //FIXME: -> Image
        Image image;
    };

    /// Caches and renders glyph for \a index
    Glyph render(uint index);

    buffer<byte> data;
    float size;
    bool hint;
    String id;

    handle<FT_FaceRec_*> face;
    float ascender, descender;
    vec2 bboxMin, bboxMax;
    map<uint, Glyph> cache;

    Map keep;
};
