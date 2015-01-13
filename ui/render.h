#pragma once
#include "graphics.h"

void fill(const Image& target, int2 origin, int2 size, bgr3f color, float alpha);

void render(const Image& target, const Graphics& graphics, vec2 offset = 0, float scale = 1);
inline Image render(Image&& target, const Graphics& graphics, vec2 offset = 0, float scale = 1) { render(target, graphics, offset, scale); return move(target); }
inline Image render(int2 size, const Graphics& graphics, vec2 offset = 0, float scale = 1) { Image target (size); target.clear(0xFF); render(target, graphics, offset, scale); return move(target); }
