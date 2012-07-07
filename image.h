#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"
#include "memory.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,uint,4> int4;

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

    T operator()(uint x, uint y) const {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    T& operator()(uint x, uint y) {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

#define generic template<class T>

/// Creates a new handle to \a image data (unsafe if freed)
generic inline Image<T> share(const Image<T>& o) { return Image<T>(o.data,o.width,o.height,o.stride,false); }
/// Copies the image buffer
generic inline Image<T> copy(const Image<T>& o) {Image<T> copy(o.width,o.height); ::copy(copy.data,o.data,o.stride*o.height); return copy;}
/// Returns a copy of the image resized to \a width x \a height
Image<byte4> resize(const Image<byte4>& image, uint width, uint height);
/// Swaps ARGB <-> BGRA in place
Image<byte4> swap(Image<byte4>&& image);
/// Flip the image around the horizontal axis in place
Image<byte4> flip(Image<byte4>&& image);
/// Decodes \a file to an Image
Image<byte4> decodeImage(const array<byte>& file);

#undef generic
