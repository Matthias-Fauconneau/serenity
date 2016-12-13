#pragma once
/// \file font.h Freetype font renderer wrapper (Font)
#include "file.h"
#include "image.h"
#include "map.h"
struct  FT_FaceRec_;

const Folder& fontFolder();
String findFont(string fontName, ref<string> fontTypes={""});

/// Font scaled to a given size (Freetype wrapper and glyph cache)
struct Font {
    /// Loads font /a data scaled to /a size pixels high
    Font(ref<byte> data, float size, bool hint=false);
    default_move(Font);
    ~Font();

    map<uint, uint> indexCache;

    /// Returns font glyph index for Unicode codepoint \a code
    uint index(uint code);
    /// Returns font glyph index for glyph \a name
    uint index(string name) const;

    // -- Size dependent
    float size;
    bool hint;
    handle<FT_FaceRec_*> face;
    float ascender = 0, descender = 0;
    vec2 bboxMin = 0, bboxMax = 0; // in font units

    struct Glyph {
     int2 offset; // (left bearing, min.y-baseline) //FIXME: -> Image
     Image image;
     Glyph() : offset(0) {}
     Glyph(int2 offset, Image&& image) : offset(offset), image(move(image)) {}
    };
    map<uint, Glyph> cache;

    /// Returns scaled kerning adjustment between \a leftIndex and \a rightIndex
    float kerning(uint leftIndex, uint rightIndex);

    struct Metrics {
	float advance;
 vec2 bearing;
	int leftOffset, rightOffset; // 26.6 bearing offset to correct kerning with hinting
	union {
	    struct { float width, height; };
     vec2 size;
	};
    };
    map<uint, Metrics> metricsCache;

    /// Returns hinted advance for \a index
    Metrics metrics(uint index);

    /// Caches and renders glyph for \a index
    Glyph render(uint index);
};

/// Holds scalable font data and a \a Font instance (glyph cache) for each size
struct FontData {
    buffer<byte> data;
    String name;
    Map keep;
    //handle<FT_FaceRec_*> face; // Unscaled (for index)
    map<float, unique<Font>> fonts;

    //FontData(buffer<byte>&& data, string name);
    FontData(buffer<byte>&& data, string name) : data(move(data)), name(copyRef(name)) {}
    FontData(Map&& data, string name) : FontData(unsafeRef(data), name) { keep=move(data); }
    FontData(const File& file, string name) : FontData(Map(file), name?:file.name()) {}
    //~FontData();

    Font& font(float size, bool hint = false) { return *(fonts.find(size) ?: &fonts.insert(size, ::unique<Font>(data, size, hint))); }

    /*// -- Size independant
    /// Returns font glyph index for Unicode codepoint \a code
    uint index(uint code) const;
    /// Returns font glyph index for glyph \a name
    uint index(string name) const;*/
};

FontData* getFont(string fontName, ref<string> fontTypes={"","R","Regular"});
