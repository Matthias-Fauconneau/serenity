#pragma once
#include "core.h"
#include "array.h"
#include "vector.h"

struct Image {
	no_copy(Image)

    byte4* data=0; bool own=false;
    int width=0, height=0;

	Image(){}
    Image(Image&& o) : data(o.data), own(o.own), width(o.width), height(o.height) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; o.data=0; return *this; }
    //Image(const byte4* data, int width, int height):data((byte4*)data),width(width),height(height){}
    Image(int width, int height):data(new byte4[width*height]),own(true),width(width),height(height){}
    Image(array<byte4>&& data, int width, int height):data((byte4*)&data),own(true),width(width),height(height) {
        assert(data.size >= width*height, data.size, width, height);
        data.capacity = 0; //taking ownership
    }
    Image(array<byte>&& file);

    ~Image(){ if(data && own) delete data; }
	operator bool() { return data; }
    Image copy() const { Image r(width,height); ::copy(r.data,data,width*height); return r; }
    Image& resize(int width, int height);
};
