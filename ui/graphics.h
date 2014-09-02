#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Fill graphic element
struct Fill {
    vec2 origin, size;
    vec3 color; float opacity;
};

/// Image graphic element
struct Blit {
    vec2 origin, size;
    Image image;
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
    uint code;
};

/// Line graphic element
struct Line {
    vec2 a, b;
};

/// Set of graphic elements
struct Graphics {
    array<Fill> fills;
    array<Blit> blits;
    array<Glyph> glyphs;
    array<Line> lines;
    void append(const Graphics& o, vec2 offset) {
        for(const auto& e: o.blits) blits << Blit{offset+e.origin, e.size, share(e.image)};
        for(auto e: o.glyphs) { e.origin += offset; glyphs << e; }
        for(auto e: o.lines) { e.a += offset; e.b += offset; lines << e; }
    }
};
