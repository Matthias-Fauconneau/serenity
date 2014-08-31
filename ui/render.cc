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

void render(const Image& target, const Graphics& graphics) {
    for(const auto& e: graphics.blits) {
        assert(e.image.width && e.image.height, e.image.size);
        if(int2(e.size) == e.image.size) blit(target, int2(round(e.origin)), e.image, 1, 1);
        else blit(target, int2(round(e.origin)), resize(int2(e.size), e.image), 1, 1);
    }
    for(const auto& e: graphics.glyphs) {
        Font::Glyph glyph = e.font.render(e.font.index(e.code));
        blit(target, int2(round(e.origin))+glyph.offset, glyph.image, 0, 1);
    }
}
