#include "graphics.h"

static void blend(const Image& target, uint x, uint y, vec3 source_linear, float opacity) {
    byte4& target_sRGB = target(x,y);
    vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
    int3 linearBlend = int3(round((0xFFF*(1-opacity))*vec3(target_linear) + (0xFFF*opacity)*source_linear));
    target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
            min(0xFF,target_sRGB.a+int(round(0xFF*opacity)))); // Additive opacity accumulation
}


static void fill(uint* target, uint stride, uint w, uint h, uint value) {
    for(uint y=0; y<h; y++) {
        for(uint x=0; x<w; x++) target[x] = value;
        target += stride;
    }
}

void fill(const Image& target, int2 origin, int2 size, vec3 color, float alpha) {
    assert_(vec3(0) <= color && color <= vec3(1));

    int2 min = ::max(int2(0), origin);
    int2 max = ::min(target.size, origin+size);
    if(max<=min) return;

    if(alpha==1) { // Solid fill
        int3 linear = int3(round(float(0xFFF)*color));
        byte4 sRGB = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
        fill((uint*)target.data+min.y*target.stride+min.x, target.stride, max.x-min.x, max.y-min.y, (uint&)sRGB);
    } else {
        for(int y: range(min.y,max.y)) for(int x: range(min.x,max.x)) blend(target, x, y, color, alpha);
    }
}


static void blit(const Image& target, int2 origin, const Image& source, vec3 color, float opacity) {
    assert_(vec3(0) <= color && color <= vec3(1));
    assert_(source);

    int2 min = ::max(int2(0), origin);
    int2 max = ::min(target.size, origin+source.size);
    if(color==vec3(0) && opacity==1 && !source.sRGB) { // Alpha multiply (e.g. glyphs)
        for(int y: range(min.y,max.y)) for(int x: range(min.x,max.x)) {
            int opacity = source(x-origin.x,y-origin.y).a; // FIXME: single channel images
            byte4& target_sRGB = target(x,y);
            vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
            int3 linearBlend = int3(round((0xFFF*(1-float(opacity)/0xFF))*vec3(target_linear)));
            target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
                    ::min(0xFF,int(target_sRGB.a)+opacity)); // Additive opacity accumulation
        }
    } else {
        for(int y: range(min.y,max.y)) for(int x: range(min.x,max.x)) {
            byte4 BGRA = source(x-origin.x,y-origin.y);
            vec3 linear = source.sRGB ? vec3(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]) : vec3(BGRA.bgr())/float(0xFF);
            blend(target, x, y, color*linear, opacity*BGRA.a/0xFF);
        }
    }
}


static void blend(const Image& target, uint x, uint y, vec3 color, float opacity, bool transpose) {
    if(transpose) swap(x,y);
    if(x>=target.width || y>=target.height) return;
    blend(target, x,y, color, opacity);
}

void line(const Image& target, vec2 p1, vec2 p2, vec3 color, float opacity) {
    assert_(vec3(0) <= color && color <= vec3(1));

    float dx = p2.x - p1.x, dy = p2.y - p1.y;
    bool transpose=false;
    if(abs(dx) < abs(dy)) { swap(p1.x, p1.y); swap(p2.x, p2.y); swap(dx, dy); transpose=true; }
    if(p1.x > p2.x) { swap(p1.x, p2.x); swap(p1.y, p2.y); }
    if(dx==0) return; //p1==p2
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(p1.x), yend = p1.y + gradient * (xend - p1.x);
        float xgap = 1 - fract(p1.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(p2.x), yend = p2.y + gradient * (xend - p2.x);
        float xgap = fract(p2.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * opacity, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * opacity, transpose);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        blend(target, x, intery, color, (1-fract(intery)) * opacity, transpose);
        blend(target, x, intery+1, color, fract(intery) * opacity, transpose);
        intery += gradient;
    }
}

void render(const Image& target, const Graphics& graphics) {
    for(const auto& e: graphics.fills) fill(target, int2(round(e.origin)), int2(e.size), e.color, e.opacity);
    for(const auto& e: graphics.blits) {
        if(int2(e.size) == e.image.size) blit(target, int2(round(e.origin)), e.image, 1, 1);
        else blit(target, int2(round(e.origin)), resize(int2(e.size), e.image), 1, 1);
    }
    for(const auto& e: graphics.glyphs) {
        Font::Glyph glyph = e.font.render(e.font.index(e.code));
        blit(target, int2(round(e.origin))+glyph.offset, glyph.image, 0, 1);
    }
    for(const auto& e: graphics.lines) line(target, e.a, e.b, 0, 1);
}
