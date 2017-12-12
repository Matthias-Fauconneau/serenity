#pragma once
/// \file render.h 2D graphics primitives (fill, blit, line)
#include "image.h"

enum Op { Src, Mul };
struct Span { float x, min, max; };

/// Primary colors
static constexpr bgr3f black {0, 0, 0};
static constexpr bgr3f red {0, 0, 1};
static constexpr bgr3f green {0, 1, 0};
static constexpr bgr3f blue {1, 0, 0};
static constexpr bgr3f white {1, 1, 1};
static constexpr bgr3f cyan {1, 1, 0};
static constexpr bgr3f magenta {1, 0, 1};
static constexpr bgr3f yellow {0, 1, 1};

struct RenderTarget2D {
    vec2 size;
    RenderTarget2D(vec2 size) : size(size) {}
    virtual void fill(vec2 origin, vec2 size, bgr3f color = 0, float opacity = 1, Op op = Src) abstract;
    virtual void blit(vec2 origin, vec2 size, const Image& image, bgr3f color = 1, float opacity = 1) abstract;
    virtual void glyph(vec2 origin, float fontSize, struct FontData& font, uint code, uint index, bgr3f color = 0, float opacity = 1) abstract;
    virtual void line(vec2 p0, vec2 p1, bgr3f color = 0, float opacity = 1, bool hint = false) abstract;
    virtual void trapezoidY(Span s0, Span s1, bgr3f color = 0, float opacity = 1) abstract;
};
