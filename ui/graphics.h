#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

/// Primary colors
static constexpr vec3 red (0, 0, 1);
static constexpr vec3 green (0, 1, 0);
static constexpr vec3 blue (1, 0, 0);
static constexpr vec3 white (1, 1, 1);

/// Fill graphic element
struct Fill {
    vec2 origin, size;
    bgr3f color; float opacity;
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
    bgr3f color;
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
    explicit operator bool() const { return fills || blits || glyphs || lines; }
    void append(const Graphics& o, vec2 offset) {
        for(auto e: o.fills) { e.origin += offset; fills.append(e); }
        for(const auto& e: o.blits) blits.append(e.origin+offset, e.size, share(e.image));
        for(auto e: o.glyphs) { e.origin += offset; glyphs.append(e); }
        for(auto e: o.lines) { e.a += offset; e.b += offset; lines.append(e); }
    }
};
inline String str(const Graphics& o) { return str(o.fills.size, o.blits.size, o.glyphs.size, o.lines.size); }
