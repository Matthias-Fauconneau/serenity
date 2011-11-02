#pragma once
#include "core.h"

struct Image {
	no_copy(Image)
	uint8* data=0; int width=0, height=0, depth=0;
	Image(){}
	Image(Image&& o) : data(o.data), width(o.width), height(o.height), depth(o.depth) { o.data=0; }
	Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; depth=o.depth; o.data=0; return *this; }
	Image(uint8* data, int width, int height, int depth):data(data),width(width),height(height),depth(depth){}
	Image(uint8* file, int size);
	~Image(){ if(data) delete data; }
	operator bool() { return data; }
};
