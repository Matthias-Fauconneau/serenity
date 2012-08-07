#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,uint,4> int4;

#define RGB565 1
#if RGB565
struct rgb565 {
    uint16 pack;
    rgb565():pack(0){}
    rgb565(uint8 i):pack( (i&0b11111000)<<8 | (i&0b11111100)<<3 | i>>3 ) {}
    rgb565(uint8 b, uint8 g, uint8 r):pack( (r&0b11111000)<<8 | (g&0b11111100)<<3 | b>>3 ) {}
    rgb565(byte4 c):rgb565(c.b, c.g, c.r){}
    operator byte4() { return byte4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
    operator   int4() { return     int4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
};
/// pixel is native display format
typedef rgb565 pixel;
#else
typedef byte4 pixel;
#endif

template<class T> struct Image {
    T* data=0;
    uint width=0, height=0, stride=0;
    bool own=false, alpha=false;

    no_copy(Image)
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own) { o.data=0; }
    Image& operator =(Image&& o) { this->~Image(); data=o.data; width=o.width; height=o.height; stride=o.stride; o.data=0; return *this; }

    Image(){}
    Image(T* data, int width, int height, int stride, bool own, bool alpha) :
        data(data),width(width),height(height),stride(stride),own(own),alpha(alpha){}
    Image(int width, int height, int stride=0);
    Image(array<T>&& data, uint width, uint height);

    ~Image();
    explicit operator bool() const { return data; }
    explicit operator ref<T>() { assert(width==stride); return ref<T>(data,height*stride); }

    T operator()(uint x, uint y) const {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    T& operator()(uint x, uint y) {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

#define generic template<class T>

generic inline string str(const Image<T>& o) { return str(o.width,"x"_,o.height); }
/// Creates a new handle to \a image data (unsafe if freed)
generic inline Image<T> share(const Image<T>& o) { return Image<T>(o.data,o.width,o.height,o.stride,false,o.alpha); }
/// Copies the image buffer
generic inline Image<T> copy(const Image<T>& o) {Image<T> copy(o.width,o.height); ::copy(copy.data,o.data,o.stride*o.height); return copy;}
/// Convert between image formats
template<class D, class S> inline Image<D> convert(const Image<S>& s) {
    if(!s) return Image<D>();
    Image<D> copy(s.width,s.height);
    for(uint x=0;x<s.width;x++) for(uint y=0;y<s.height;y++) copy(x,y)=s(x,y);
    return copy;
}
/// template specialization to convert alpha to opaque white background
template<> inline Image<pixel> convert<pixel,byte4>(const Image<byte4>& source);
/// Returns a copy of the image resized to \a width x \a height
Image<byte4> resize(const Image<byte4>& image, uint width, uint height);
/// Flip the image around the horizontal axis in place
Image<byte4> flip(Image<byte4>&& image);
/// Decodes \a file to an Image
Image<byte4> decodeImage(const ref<byte>& file);

#undef generic

/// Declare a small .png icon embedded in the binary, accessible at runtime as an Image (lazily decoded)
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
    extern byte _binary_icons_## name ##_png_start[]; \
    extern byte _binary_icons_## name ##_png_end[]; \
    static const Image<byte4>& name ## Icon() { \
      static Image<byte4> icon = decodeImage(array<byte>(_binary_icons_## name ##_png_start, \
                                                                                               _binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start)); \
      return icon; \
    }
