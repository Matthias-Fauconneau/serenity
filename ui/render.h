#pragma once
/// \file render.h 2D graphics primitives (fill, blit, line)
#include "image.h"

struct Span { float x, min, max; };

struct RenderTarget2D {
    vec2 size;
    RenderTarget2D(vec2 size) : size(size) {}
    virtual void fill(vec2 origin, vec2 size, bgr3f color = 0, float opacity = 1) abstract;
    virtual void blit(vec2 origin, vec2 size, const Image& image, bgr3f color = 1, float opacity = 1) abstract;
    virtual void glyph(vec2 origin, float fontSize, struct FontData& font, uint code, uint index, bgr3f color = 0, float opacity = 1) abstract;
    virtual void line(vec2 p0, vec2 p1, bgr3f color = 0, float opacity = 1, bool hint = false) abstract;
    virtual void trapezoidY(Span s0, Span s1, bgr3f color = 0, float opacity = 1) abstract;
};
