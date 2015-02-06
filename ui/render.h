#pragma once
#include "graphics.h"

void fill(const Image& target, int2 origin, int2 size, bgr3f color, float alpha);

void render(const Image& target, const Graphics& graphics, vec2 offset = 0);
inline Image render(int2 size, const Graphics& graphics, vec2 offset = 0) {
	Image target (size);
	//target.clear(0xFF);
	target.clear(0);
	target.alpha = true;
	render(target, graphics, offset);
	return move(target);
}
