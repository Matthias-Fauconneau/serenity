#pragma once
#include "graphics.h"

void fill(const Image& target, int2 origin, int2 size, bgr3f color, float alpha);

// Oxygen-like radial gradient background
void oxygen(const Image& target, int2 min, int2 max);

void render(const Image& target, const Graphics& graphics);
