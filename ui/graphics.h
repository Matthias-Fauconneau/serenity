#pragma once
/// \file graphics.h 2D graphics primitives (fill, blit, line)
#include "image.h"
#include "font.h"

// Colors //

constexpr vec3 black(0, 0, 0);
constexpr vec3 darkGray(12./16, 12./16, 12./16);
constexpr vec3 lightGray (15./16, 15./16, 15./16);
constexpr vec3 white (1, 1, 1);
constexpr vec3 highlight (14./16, 12./16, 8./16);
constexpr vec3 blue (1, 0, 0);
constexpr vec3 green (0, 1, 0);
constexpr vec3 red (0, 0, 1);

/// Converts lightness, chroma, hue to linear sRGB
/// sRGB primaries:
/// Red: L~53.23, C~179.02, h~0.21
/// Green: L~87.74, C~135.80, h~2.23
/// Blue: L~32.28, C~130.61, h~-1.64
vec3 LChuvtoBGR(float L, float C, float h);

// Immediate rendering //

/// Blends pixel at \a x, \a y with \a color
//void blend(const Image& target, uint x, uint y, vec3 color, float alpha);

/// Fills pixels inside \a rect with \a color
void fill(const Image& target, Rect rect, vec3 color=black, float alpha=1);

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(const Image& target, int2 position, const Image& source, vec3 color=white, float alpha=1);

/// Draws a thin antialiased line from p1 to p2
void line(const Image& target, vec2 p1, vec2 p2, vec3 color=black, float alpha=1);
inline void line(const Image& target, int2 p1, int2 p2, vec3 color=black, float alpha=1) { line(target, vec2(p1), vec2(p2), color, alpha); }

/// Draws a parallelogram parallel to the Y axis
void parallelogram(const Image& target, int2 p0, int2 p1, int dy, vec3 color=black, float alpha=1);

// Draws a filled cubic spline loop (two control points between each end point)
void cubic(const Image& target, const ref<vec2>& points, vec3 color=black, float alpha=1, const uint oversample = 8);

// Deferred rendering //

/// Image graphic element
struct Blit {
    vec2 origin;
    Image image;
    void render(const Image& target) const {
        blit(target, int2(round(origin)), image);
    }
};

/// Text graphic element
struct Glyph {
    vec2 origin;
    Font& font;
    uint code;
    void render(const Image& target) const {
        Font::Glyph glyph = font.render(font.index(code));
        blit(target, int2(round(origin))+glyph.offset, glyph.image, black);
    }
};

/// Set of graphic elements
struct Graphics {
    array<Blit> blits;
    array<Glyph> glyphs;
    Graphics& append(const Graphics& o, vec2 offset) {
        for(const auto& e: o.blits) blits << Blit{offset+e.origin, share(e.image)};
        for(auto e: o.glyphs) { e.origin += offset; glyphs << e; }
        return *this;
    }
    void render(const Image& target) const {
        for(const auto& e: blits) e.render(target);
        for(const auto& e: glyphs) e.render(target);
    }
};
