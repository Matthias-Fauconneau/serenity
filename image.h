#pragma once
#include "array.h"
#include "vector.h"

struct Image {
    no_copy(Image)

    byte4* data=0;
    uint width=0, height=0;
    bool own=false;

    Image(){}
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), own(o.own) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; o.data=0; return *this; }
    Image(byte4* data, int width, int height):data(data),width(width),height(height),own(false){}
    Image(int width, int height):data(allocate<byte4>(width*height)),width(width),height(height),own(true){}
    Image(array<byte4>&& data, uint width, uint height):data((byte4*)data.data()),width(width),height(height),own(true) {
        assert(data.size() >= width*height, data.size(), width, height);
        data.buffer.capacity = 0; //taking ownership
    }
    explicit Image(array<byte>&& file);

    ~Image(){ if(data && own) delete data; }
    explicit operator bool() const { return data; }

    byte4 operator()(uint x, uint y) const {assert(x<width && y<height); return data[y*width+x]; }
    byte4& operator()(uint x, uint y) {assert(x<width && y<height); return data[y*width+x]; }
    int2 size() const { return int2(width,height); }
};
inline Image copy(const Image& image) { Image copy(image.width,image.height); ::copy(copy.data,image.data,image.width*image.height); return copy; }
/// Returns a copy of the image resized to \a width x \a height
Image resize(const Image& image, uint width, uint height);
/// Swap ARGB <-> BGRA in place
Image swap(Image&& image);

/// Convenience function to iterate all pixels of an image
#define for_Image(image) for(int y=0,h=image.height;y<h;y++) for(int x=0,w=image.width;x<w;x++)
