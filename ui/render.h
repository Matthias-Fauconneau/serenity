#pragma once
#include "graphics.h"

void blend(const Image& target, uint x, uint y, bgr3f source_linear, float opacity);
void fill(const Image& target, int2 origin, int2 size, bgr3f color=black, float opacity=1);
void blit(const Image& target, int2 origin, const Image& source, bgr3f color=1, float opacity=1);
void line(const Image& target, vec2 p1, vec2 p2, bgr3f color=black, float opacity=1, bool hint=false);

void render(const Image& target, const Graphics& graphics, vec2 offset = 0);
inline Image render(int2 size, const Graphics& graphics, vec2 offset = 0) {
 Image target (size);
 target.clear(0xFF);
 target.alpha = false;
 render(target, graphics, offset);
 return move(target);
}
