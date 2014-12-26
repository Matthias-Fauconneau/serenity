#pragma once
#include "graphics.h"

void fill(const Image& target, int2 origin, int2 size, bgr3f color, float alpha);

void render(const Image& target, const Graphics& graphics, vec2 offset = 0);
