#pragma once
#include "render.h"

struct PDFRenderTarget : RenderTarget2D {
    PDFRenderTarget(vec2 pageSize) : RenderTarget2D(pageSize) {}
    virtual void fill(vec2 origin, vec2 size, bgr3f color, float opacity) override;
    virtual void blit(vec2 origin, vec2 size, Image&& image, bgr3f color, float opacity);
    virtual void glyph(vec2 origin, float fontSize, FontData& font, uint unused code, uint index, bgr3f color, float opacity);
    virtual void line(vec2 p0, vec2 p1, bgr3f color, float opacity, bool hint) override;
    virtual void trapezoidY(Span s0, Span s1, bgr3f color, float opacity) override;
};
