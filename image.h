#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,uint,4> int4;

struct Image {
    const byte4* data=0;
    uint width=0, height=0, stride=0;
    bool own=false, alpha=false;

    no_copy(Image)
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own), alpha(o.alpha) { o.data=0; }
    Image& operator =(Image&& o) {  this->~Image(); data=o.data; o.data=0;
        width=o.width; height=o.height; stride=o.stride;  own=o.own; alpha=o.alpha; return *this; }

    Image(){}
    Image(byte4* data, int width, int height, int stride, bool own, bool alpha) :
        data(data),width(width),height(height),stride(stride),own(own),alpha(alpha){}
    Image(int width, int height, bool alpha=false, int stride=0)
        : data(allocate<byte4>(height*(stride?:width))), width(width), height(height), stride(stride?:width), own(true), alpha(alpha) {
        assert(width); assert(height);
    }
    Image(array<byte4>&& o, uint width, uint height, bool alpha) : data(o.data()),width(width),height(height),stride(width),own(true),alpha(alpha) {
        assert(o.size() == width*height, o.size(), width, height); assert(o.tag==-2); o.tag = 0;
    }

    ~Image(){ if(data && own) { unallocate(data,height*stride); } }
    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }

    byte4 operator()(uint x, uint y) const {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    byte4& operator()(uint x, uint y) {assert(x<width && y<height,int(x),int(y),width,height); return (byte4&)data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

inline string str(const Image& o) { return str(o.width,"x"_,o.height); }
/// Creates a new handle to \a image data (unsafe if freed)
inline Image share(const Image& o) { return Image((byte4*)o.data,o.width,o.height,o.stride,false,o.alpha); }
/// Copies the image buffer
inline Image copy(const Image& o) {Image r(o.width,o.height,o.alpha); ::copy((byte4*)r.data,o.data,o.stride*o.height); return r;}
/// Returns a copy of the image resized to \a width x \a height
Image resize(const Image& image, uint width, uint height);
/// Flip the image around the horizontal axis in place
inline Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++)  swap(image(x,y),image(x,h-1-y));
    return move(image);
}
/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declare a small .png icon embedded in the binary, accessible at runtime as an Image (lazily decoded)
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
static const Image& name ## Icon() { \
    extern byte _binary_icons_## name ##_png_start[]; extern byte _binary_icons_## name ##_png_end[]; \
    static Image icon = decodeImage(array<byte>(_binary_icons_## name ##_png_start, _binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start)); \
    return icon; \
}
