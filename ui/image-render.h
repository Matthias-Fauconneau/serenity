#pragma once
#include "render.h"

void blend(byte4& target, bgr3f source_linear, float opacity);

#if 0
void fill(const Image& target, int2 origin, uint2 size, bgr3f color, float alpha);
#endif
void line(const Image& target, vec2 p0, vec2 p1, bgr3f color = 0_, float opacity = 1/*, bool hint = false*/);
void blit(const Image& target, const Image& source, int2 origin= 0_, uint2 size=uint2(-1), bgr3f color = 1_, float opacity = 1);
#if 0
void glyph(const Image& target, int2 origin, float fontSize, FontData& font, uint index, bgr3f color = 0_, float opacity = 1);
void trapezoidY(const Image& target, Span a, Span b, bgr3f color = 0_, float opacity = 1);
#endif

struct ImageRenderTarget : Image, RenderTarget2D {
    ImageRenderTarget(Image&& image) : Image(::move(image)), RenderTarget2D(vec2(image.size)) {}
    virtual ~ImageRenderTarget() {}
    //virtual void fill(vec2, vec2, bgr3f, float) override { error("UNIMPL fill"); }
    //virtual void line(vec2, vec2, bgr3f, float, bool) override { error("UNIMPL line"); }
    //virtual void blit(vec2, vec2, Image&&, bgr3f, float) override { error("UNIMPL blit"); }
    //virtual void glyph(vec2, float, FontData&, uint, uint, bgr3f, float) override { error("UNIMPL glyph"); }
    //virtual void trapezoidY(Span, Span, bgr3f, float) override { error("UNIMPL trapezoidY"); }
    /*virtual void fill(vec2 origin, vec2 size, bgr3f color, float opacity) override {
        ::fill(*this, int2(origin), uint2(size), color, opacity);
    }*/
    virtual void line(vec2 p0, vec2 p1, bgr3f color, float opacity/*, bool hint*/) override {
        ::line(*this, p0, p1, color, opacity/*, hint*/);
    }
    virtual void blit(Image&& image, vec2 origin, vec2 size, bgr3f color, float opacity) override {
        ::blit(*this, image, int2(origin), uint2(size), color, opacity);
    }
    /*virtual void glyph(vec2 origin, float fontSize, FontData& font, unused uint code, uint index, bgr3f color, float opacity) override {
        ::glyph(*this, int2(origin), fontSize, font, index, color, opacity);
    }
    virtual void trapezoidY(Span s0, Span s1, bgr3f color, float opacity) override {
        ::trapezoidY(*this, s0, s1, color, opacity);
    }*/
};
