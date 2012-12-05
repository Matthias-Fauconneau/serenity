#include "display.h"

array<Rect> clipStack;
Rect currentClip=Rect(0);
SHADER(fill);
SHADER(blit);

void fill(Rect rect, vec4 color) {
    rect = rect & currentClip;
    glBlend(color.w!=1, true);
    fillShader()["color"] = color;
    glDrawRectangle(fillShader(), rect);
}

void blit(int2 target, const GLTexture& source, vec4 color) {
    glBlend(source.alpha, true);
    blitShader()["color"] = color;
    blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(source);
    glDrawRectangle(blitShader(), target+Rect(source.size()), true);
}

void substract(int2 target, const GLTexture& source, vec4 color) {
    glBlend(true, false);
    blitShader()["color"] = vec4(1)-color;
    blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(source);
    glDrawRectangle(blitShader(), target+Rect(source.size()), true);
}

void line(vec2 p1, vec2 p2, vec4 color) {
    fillShader()["color"] = vec4(vec3(1)-color.xyz(),1.f);
    glDrawLine(fillShader(), p1, p2);
}
