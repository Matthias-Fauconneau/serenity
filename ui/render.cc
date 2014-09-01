#include "graphics.h"

static void blend(const Image& target, uint x, uint y, vec3 source_linear, float alpha) {
    byte4& target_sRGB = target(x,y);
    vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
    int3 linearBlend = int3(round((0xFFF*(1-alpha))*vec3(target_linear) + (0xFFF*alpha)*source_linear));
    target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
            min(0xFF,target_sRGB.a+int(round(0xFF*alpha)))); // Additive alpha accumulation
}

static void blit(const Image& target, int2 position, const Image& source, vec3 color, float alpha) {
    assert_(source);
    Rect rect = (position+Rect(source.size)) & Rect(target.size);
    color = clip(vec3(0), color, vec3(1));
    if(color!=vec3(0) || alpha<1 || source.sRGB) {
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            byte4 BGRA = source(x-position.x,y-position.y);
            vec3 linear = source.sRGB ? vec3(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]) : vec3(BGRA.bgr())/float(0xFF);
            blend(target, x, y, color*linear, alpha*BGRA.a/0xFF);
        }
    } else { // Alpha multiply (e.g. glyphs)
        for(int y: range(rect.min.y,rect.max.y)) for(int x: range(rect.min.x,rect.max.x)) {
            int alpha = source(x-position.x,y-position.y).a; // FIXME: single channel images
            byte4& target_sRGB = target(x,y);
            vec3 target_linear(sRGB_reverse[target_sRGB[0]], sRGB_reverse[target_sRGB[1]], sRGB_reverse[target_sRGB[2]]);
            int3 linearBlend = int3(round((0xFFF*(1-float(alpha)/0xFF))*vec3(target_linear)));
            target_sRGB = byte4(sRGB_forward[linearBlend[0]], sRGB_forward[linearBlend[1]], sRGB_forward[linearBlend[2]],
                    min(0xFF,int(target_sRGB.a)+alpha)); // Additive alpha accumulation
        }
    }
}


static void blend(const Image& target, uint x, uint y, vec3 color, float alpha, bool transpose) {
    if(transpose) swap(x,y);
    if(x>=target.width || y>=target.height) return;
    blend(target, x,y, color, alpha);
}

void line(const Image& target, vec2 p1, vec2 p2, vec3 color, float alpha) {
    color = clip(vec3(0), color, vec3(1));
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
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * alpha, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * alpha, transpose);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(p2.x), yend = p2.y + gradient * (xend - p2.x);
        float xgap = fract(p2.x + 1./2);
        blend(target, xend, yend, color, (1-fract(yend)) * xgap * alpha, transpose);
        blend(target, xend, yend+1, color, fract(yend) * xgap * alpha, transpose);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        blend(target, x, intery, color, (1-fract(intery)) * alpha, transpose);
        blend(target, x, intery+1, color, fract(intery) * alpha, transpose);
        intery += gradient;
    }
}

void render(const Image& target, const Graphics& graphics) {
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
