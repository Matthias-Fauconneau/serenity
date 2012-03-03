#pragma once
#include "array.h"
#include "vector.h"

struct Image {
    no_copy(Image)

    byte4* data=0;
    int width=0, height=0;
    bool own=false;

    Image(){}
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), own(o.own) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; o.data=0; return *this; }
    Image(byte4* data, int width, int height):data(data),width(width),height(height),own(false){}
    Image(int width, int height):data(allocate<byte4>(width*height)),width(width),height(height),own(true){}
    Image(array<byte4>&& data, int width, int height):data((byte4*)&data),width(width),height(height),own(true) {
        assert(data.size() >= width*height, data.size(), width, height);
        data.buffer.capacity = 0; //taking ownership
    }
    Image(array<byte>&& file);

    ~Image(){ if(data && own) delete data; }
    operator bool() { return data; }

    byte4 operator()(int x, int y) const { return data[y*width+x]; }
    byte4& operator()(int x, int y) { return data[y*width+x]; }

    Image copy() const { Image r(width,height); if(data) ::copy(r.data,data,width*height); return r; }
    /// Resize this image to \a width x \a height
    Image& resize(int width, int height);
    /// Swap ARGB <-> BGRA
    Image& swap();
};
/// Convenience function to iterate all pixels of an image
#define for_Image(image) for(int y=0,h=image.height;y<h;y++) for(int x=0,w=image.width;x<w;x++)
