#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,uint,4> int4;

#define RGB565 __arm__
#if RGB565
struct rgb565 {
    uint16 pack;
    constexpr rgb565():pack(0){}
    constexpr rgb565(uint8 i):pack( (i&0b11111000)<<8 | (i&0b11111100)<<3 | i>>3 ) {}
    constexpr rgb565(uint8 b, uint8 g, uint8 r, uint8 unused a):pack((r&0b11111000)<<8|(g&0b11111100)<<3|b>>3){}
    constexpr rgb565(byte4 c):rgb565(c.b, c.g, c.r){}
    constexpr rgb565(int4 c):rgb565(c.b, c.g, c.r){}
    operator byte4() const { return byte4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
    operator    int4() const { return     int4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
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
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own), alpha(o.alpha) { o.data=0; }
    Image& operator =(Image&& o) {  this->~Image(); data=o.data; o.data=0;
        width=o.width; height=o.height; stride=o.stride;  own=o.own; alpha=o.alpha; return *this; }

    Image(){}
    Image(T* data, int width, int height, int stride, bool own, bool alpha) :
        data(data),width(width),height(height),stride(stride),own(own),alpha(alpha){}
    Image(int width, int height, bool alpha=false, int stride=0)
        : data(allocate<T>(height*(stride?:width))), width(width), height(height), stride(stride?:width), own(true), alpha(alpha) {
        debug( clear((byte*)data,height*stride*sizeof(T)); )
        assert(width); assert(height);
    }
    Image(array<T>&& data, uint width, uint height) : data((T*)data.data()),width(width),height(height),stride(width),own(true) {
        assert(data.size() >= width*height, data.size(), width, height);
        assert(data.buffer.capacity);
        data.buffer.capacity = 0; //taking ownership
    }

    ~Image(){ if(data && own) { unallocate(data,height*stride); } }
    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(T)); }

    T operator()(uint x, uint y) const {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    T& operator()(uint x, uint y) {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

#define generic template<class T>

generic string str(const Image<T>& o) { return str(o.width,"x"_,o.height); }
/// Creates a new handle to \a image data (unsafe if freed)
generic Image<T> share(const Image<T>& o) { return Image<T>(o.data,o.width,o.height,o.stride,false,o.alpha); }
/// Copies the image buffer
generic Image<T> copy(const Image<T>& o) {Image<T> copy(o.width,o.height,o.alpha); ::copy(copy.data,o.data,o.stride*o.height); return copy;}
/// Returns a copy of the image resized to \a width x \a height
Image<byte4> resize(const Image<byte4>& image, uint width, uint height);
/// Flip the image around the horizontal axis in place
generic Image<T> flip(Image<T>&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++)  swap(image(x,y),image(x,h-1-y));
    return move(image);
}
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
