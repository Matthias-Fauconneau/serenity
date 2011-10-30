#pragma once
#include "common.h"

struct Image {
	uint8* data=0; int width=0, height=0, depth=0;
	Image()=default;
	Image(uint8* data, int width, int height, int depth):data(data),width(width),height(height),depth(depth){}
	Image(uint8* file, int size);
	operator bool() { return data; }
};
