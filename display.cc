#include "display.h"

array<Rect> clipStack;
Rect currentClip=Rect(0);
SHADER(fill);
SHADER(blit);

void fill(Rect rect, vec4 color, bool blend) {
    rect = rect & currentClip;
    glBlend(blend, true);
    fillShader()["color"] = color;
    glDrawRectangle(fillShader(), rect);
}

void blit(int2 target, const GLTexture& source, float opacity unused) {
    /*Rect rect = (target+Rect(source.size())) & currentClip;
    glBlend(source.alpha, true);
    //blitShader()["color"] = vec4(1,1,1,opacity);
    blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(source);
    glDrawRectangle(blitShader(), rect, true);*/
}

void substract(int2 target, const GLTexture& source, vec4 color unused) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    glBlend(true, false);
    //displayShader()["color"] = vec4(1)-color;
    blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(source);
    glDrawRectangle(blitShader(), rect, true);
}

void line(vec2 p1 unused, vec2 p2 unused, vec4 color unused) {
    //TODO
}
