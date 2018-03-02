#include "image-render.h"
#include "font.h"
#include "math.h"

static void blend(const Image& target, uint x, uint y, bgr3f source_linear, float opacity) {
    byte4& target_sRGB = target(x,y);
    bgr3f target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
    bgr3i linearBlend = bgr3i(round((0xFFF*(1-opacity))*target_linear + (0xFFF*opacity)*source_linear));
    target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
            min(0xFF,target_sRGB.a+int(round(0xFF*opacity)))); // Additive opacity accumulation
}


static void fill(uint* target, uint stride, uint w, uint h, uint value) {
    for(uint unused y: range(h)) {
        for(uint x: range(w)) target[x] = value;
        target += stride;
    }
}

void fill(const Image& target, int2 origin, uint2 size, bgr3f color, float opacity) {
    assert_(bgr3f(0) <= color && color <= bgr3f(1));

    int2 min = ::max(int2(0), origin);
    int2 max = ::min(int2(target.size), origin+int2(size));
    if(max<=min) return;

    if(opacity==1) { // Solid fill
        if(!(min < max)) return;
        bgr3i linear = bgr3i(round(float(0xFFF)*color));
        byte4 sRGB = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
        fill((uint*)target.data+min.y*target.stride+min.x, target.stride, max.x-min.x, max.y-min.y, (uint&)sRGB);
    } else {
        for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) blend(target, x, y, color, opacity);
    }
}

void blit(const Image& target, int2 origin, uint2 size, const Image& source, bgr3f color, float opacity) {
    assert_(size == source.size);
    assert_(bgr3f(0) <= color && color <= bgr3f(1));
    assert_(source);

    int2 min = ::max(int2(0), origin);
    int2 max = ::min(int2(target.size), origin+int2(source.size));
    /**/  if(color==bgr3f(1) && opacity==1 && !source.alpha) { // Copy
        for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
            byte4 s = source(x-origin.x, y-origin.y);
            target(x,y) = byte4(s[0], s[1], s[2], 0xFF);
        }
    }
    else if(color==bgr3f(0) && opacity==1) { // Alpha multiply (e.g. glyphs)
        for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
            int opacity = source(x-origin.x,y-origin.y).a; // FIXME: single channel images
            byte4& target_sRGB = target(x,y);
            vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
            int3 linearBlend = int3( round((0xFFF*(1-float(opacity)/0xFF))
                                           * vec3(target_linear)));
            target_sRGB = byte4(sRGB_forward[linearBlend[0]],
                    sRGB_forward[linearBlend[1]],
                    sRGB_forward[linearBlend[2]],
                    ::min(0xFF,int(target_sRGB.a)+opacity)); // Additive opacity accumulation
        }
    }
    else {
        for(int y: range(min.y, max.y)) for(int x: range(min.x, max.x)) {
            byte4 BGRA = source(x-origin.x,y-origin.y);
            bgr3f linear = bgr3f(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]);
            blend(target, x, y, color*linear, opacity*BGRA.a/0xFF);
        }
    }
}

void glyph(const Image& target, int2 origin, float fontSize, FontData& font, uint index, bgr3f color, float opacity) {
    Font::Glyph glyph = font.font(fontSize).render(index);
    if(glyph.image) blit(target, int2(origin)+glyph.offset, glyph.image.size, glyph.image, color, opacity);
}

static void blend(const Image& target, uint x, uint y, bgr3f color, float opacity, bool transpose) {
    if(transpose) swap(x,y);
    if(x >= uint(target.size.x) || y >= uint(target.size.y)) return;
    blend(target, x,y, color, opacity);
}

void line(const Image& target, vec2 p1, vec2 p2, bgr3f color, float opacity, bool hint) {
    //if(hint && p1.y == p2.y) p1.y = p2.y = round(p1.y); // Hints
    if(hint) { // TODO: preprocess
        if(p1.x == p2.x) fill(target, int2(round(p1)), uint2(1, p2.y-p1.y), color, opacity);
        else if(p1.y == p2.y) fill(target, int2(round(p1)), uint2(p2.x-p1.x, 1), color, opacity);
        else error("");
        return;
    }
    //if(p1.x >= target.size.x || p2.x < 0) return; // Assumes p1.x < p2.x
    assert(bgr3f(0) <= color && color <= bgr3f(1));

    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(p1.x, p1.y); swap(p2.x, p2.y); swap(dx, dy); transpose=true; }
    if(p1.x > p2.x) { swap(p1.x, p2.x); swap(p1.y, p2.y); }
    if(dx==0) return; //p1==p2
    float gradient = dy / dx;
    int i1; float intery;
    {
        float xend = round(p1.x), yend = p1.y + gradient * (xend - p1.x);
        float xgap = 1 - fract(p1.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    int i2;
    {
        float xend = round(p2.x), yend = p2.y + gradient * (xend - p2.x);
        float xgap = fract(p2.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
        i2 = int(xend);
    }
    int x = i1+1;
    if(x < 0) { intery += (0-x) * gradient; x = 0; }
    for(;x<min(int(transpose ? target.size.y : target.size.x), i2); x++) {
        blend(target, x, intery, color, (1-fract(intery)) * opacity, transpose);
        blend(target, x, intery+1, color, fract(intery) * opacity, transpose);
        intery += gradient;
    }
}

void trapezoidY(const Image& target, Span s0, Span s1, bgr3f color, float opacity) {
    if(s0.x > s1.x) swap(s0, s1);
    for(uint x: range(max(0, int(s0.x)), min(int(target.size.x), int(s1.x)))) {
        float y0 = float(s0.min) + float((s1.min - s0.min) * int(x - s0.x)) / float(s1.x - s0.x); // FIXME: step
        float f0 = floor(y0);
        int i0 = int(f0);
        float y1 = float(s0.max) + float((s1.max - s0.max) * int(x - s0.x)) / float(s1.x - s0.x); // FIXME: step
        float f1 = floor(y1);
        int i1 = int(f1);
        if(uint(i0)<target.size.y) blend(target, x, i0, color, opacity*(1-(y0-f0)));
        for(uint y: range(max(0,i0+1), min(int(target.size.y),i1))) { // FIXME: clip once
            blend(target, x,y, color, opacity); // FIXME: antialias first last column
        }
        if(uint(i1)<target.size.y) blend(target, x, i1, color, opacity*(y1-f1));
    }
}
