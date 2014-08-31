#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

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

/// Set of graphic elements
struct Graphics {
    array<Blit> blits;
    array<Glyph> glyphs;
    Graphics& append(const Graphics& o, vec2 offset) {
        for(const auto& e: o.blits) blits << Blit{offset+e.origin, e.size, share(e.image)};
        for(auto e: o.glyphs) { e.origin += offset; glyphs << e; }
        return *this;
    }
};
