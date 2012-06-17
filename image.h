#pragma once
#include "array.h"
#include "vector.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,int,4> int4;

struct Image {
    no_copy(Image)

    byte4* data=0;
    uint width=0, height=0;
    bool own=false;

    Image(){}
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), own(o.own) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; o.data=0; return *this; }
    Image(byte4* data, int width, int height,bool own):data(data),width(width),height(height),own(own){}
    Image(int width, int height);
    Image(array<byte4>&& data, uint width, uint height);

    ~Image(){ if(data && own) { delete data; data=0; } }
    explicit operator bool() const { return data; }

    byte4 get(uint x, uint y) const { if(x>=width||y>=height) return byte4(0,0,0,0); return data[y*width+x]; }
    byte4 operator()(uint x, uint y) const {assert_(x<width && y<height); return data[y*width+x]; }
    byte4& operator()(uint x, uint y) {assert_(x<width && y<height); return data[y*width+x]; }
    int2 size() const { return int2(width,height); }
};
inline Image copy(const Image& image) { Image copy(image.width,image.height); ::copy(copy.data,image.data,image.width*image.height); return copy; }

/// Returns a copy of the image resized to \a width x \a height
Image resize(const Image& image, uint width, uint height);

/// Swaps ARGB <-> BGRA in place
Image swap(Image&& image);

/// Flip the image around the horizontal axis in place
Image flip(Image&& image);

/// Convenience function to iterate all pixels of an image
#define for_Image(image) for(int y=0,h=image.height;y<h;y++) for(int x=0,w=image.width;x<w;x++)

/// Decodes \a file to an Image
Image decodeImage(const array<byte>& file);
