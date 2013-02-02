#pragma once
/// \file image.h Image container and operations
#include "vector.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,int,4> int4;

struct Image {
    const byte4* data=0;
    uint width=0, height=0, stride=0;
    bool own=false, alpha=false;

    move_operator(Image):data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own), alpha(o.alpha) { o.data=0; }

    Image(){}
    Image(byte4* data, uint width, uint height, uint stride, bool own, bool alpha) : data(data),width(width),height(height),stride(stride),own(own),alpha(alpha){}
    Image(uint width, uint height, bool alpha=false, int stride=0)
        : data(allocate<byte4>(height*(stride?:width))), width(width), height(height), stride(stride?:width), own(true), alpha(alpha) {
        assert(width); assert(height);
    }
    Image(array<byte4>&& o, uint width, uint height, bool alpha) : data(o.data()),width(width),height(height),stride(width),own(true),alpha(alpha) {
        assert(width && height && o.size() == width*height, width, height, o.size()); assert(o.tag==-2); o.tag = 0;
    }

    ~Image(){ if(data && own) { unallocate(data); } }
    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }

    byte4 operator()(uint x, uint y) const {assert(x<stride && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    byte4& operator()(uint x, uint y) {assert(x<stride && y<height,int(x),int(y),width,height); return (byte4&)data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

inline string str(const Image& o) { return str(o.width,"x"_,o.height); }
/// Creates a new handle to \a image data (unsafe if freed)
inline Image share(const Image& o) { return Image((byte4*)o.data,o.width,o.height,o.stride,false,o.alpha); }
/// Copies the image buffer
inline void copy(Image& dst, const Image& src) {assert(dst.size()==src.size() && dst.stride==src.stride); ::copy((byte4*)dst.data,src.data,src.height*src.stride); }
template<> inline Image copy(const Image& src) {Image dst(src.width,src.height,src.alpha); ::copy(dst,src); return dst;}
/// Flip the image around the horizontal axis in place
Image flip(Image&& image);
/// Returns a copy of the image resized to \a width x \a height
Image resize(const Image& image, uint width, uint height);
/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declare a small .png icon embedded in the binary, accessible at runtime as an Image (lazily decoded)
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
static Image name ## Icon() { \
    extern byte _binary_icons_## name ##_png_start[]; extern byte _binary_icons_## name ##_png_end[]; \
    return decodeImage(array<byte>(_binary_icons_## name ##_png_start, _binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start)); \
}
