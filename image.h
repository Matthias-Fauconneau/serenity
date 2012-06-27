#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"
byte* allocate_(int size);

//template<class T> struct bgra { T b,g,r,a; };
//typedef vector<bgra,uint8,4> byte4;
//typedef vector<bgra,int,4> int4;

struct gray {
    uint8 level;
    gray(uint8 level):level(level){}
    //operator byte4() { return byte4(level,level,level,255); }
    //operator int4() { return int4(level,level,level,255); }
};
const gray white = gray(255);
const gray black = gray(0);

struct rgb565 {
    uint16 pack;
    rgb565():pack(0){}
    rgb565(gray g):pack( (g.level&0b11111000)<<8 | (g.level&0b11111100)<<3 | g.level>>3 ) {}
    rgb565(uint8 r, uint8 g, uint8 b):pack( (r&0b11111000)<<8 | (g&0b11111100)<<3 | b>>3 ) {}
    //rgb565(byte4 c):rgb565(c.r, c.g, c.b){}
    //operator byte4() { return byte4( (pack>>8)&0b11111000, (pack>>3)&0b11111100, pack<<3, 255); }
    //  operator int4() { return int4( (pack>>8)&0b11111000, (pack>>3)&0b11111100, (pack<<3)&0b11111000, 255); }
};

template<class T> struct Image {
    no_copy(Image)

    T* data=0;
    uint width=0, height=0, stride=0;
    bool own=false;

    Image(){}
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; stride=o.stride; o.data=0; return *this; }
    Image(T* data, int width, int height, int stride, bool own):data(data),width(width),height(height),stride(stride),own(own){}
    Image(int width, int height) : data((T*)allocate_(sizeof(T)*width*height)), width(width), height(height), stride(width), own(true) {}
    Image(array<T>&& data, uint width, uint height);

    ~Image(){ if(data && own) { delete data; data=0; } }
    explicit operator bool() const { return data; }

    T operator()(uint x, uint y) const {assert(x<width && y<height,x,y,width,height); return data[y*stride+x]; }
    T& operator()(uint x, uint y) {assert(x<width && y<height,x,y,width,height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};
/// Creates a new handle to \a image data (unsafe if freed)
template<class T> inline Image<T> share(const Image<T>& o) {
    return Image<T>(o.data,o.width,o.height,o.stride,false);
}
/*inline Image copy(const Image& image) {
    Image copy(image.width,image.height); ::copy(copy.data,image.data,image.stride*image.height); return copy;
}

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
*/

#define RGB565 1
#if RGB565
typedef rgb565 rgb;
#else
typedef byte4 rgb;
#endif
typedef Image<rgb> Pixmap; //Image in display format
