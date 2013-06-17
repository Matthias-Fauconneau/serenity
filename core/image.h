#pragma once
/// \file image.h Image container and operations
#include "vector.h"

generic struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;

struct Image {
    Image(){}
    Image(::buffer<byte4>&& buffer, byte4* data, uint width, uint height, uint stride, bool alpha) : buffer(move(buffer)),data(data),width(width),height(height),stride(stride),alpha(alpha){}
    Image(uint width, uint height, bool alpha=false, uint stride=0) : width(width), height(height), stride(stride?:width), alpha(alpha) {
        assert(width); assert(height);
        buffer=::buffer<byte4>(height*(stride?:width)); data=buffer.begin();
    }

    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }

    byte4 operator()(uint x, uint y) const {assert(x<stride && y<height,int(x),int(y),stride,width,height); return data[y*stride+x]; }
    byte4& operator()(uint x, uint y) {assert(x<stride && y<height,int(x),int(y),stride,width,height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }

    ::buffer<byte4> buffer; //FIXME: shared
    byte4* data=0; // First pixel
    uint width=0, height=0, stride=0;
    bool own=false, alpha=false;
};
inline String str(const Image& o) { return str(o.width,"x"_,o.height); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline Image share(const Image& o) { return Image(unsafeReference(o.buffer),o.data,o.width,o.height,o.stride,o.alpha); }

/// Copies the image buffer
template<> inline Image copy(const Image& src) { return Image(copy(src.buffer), src.data, src.width,src.height,src.stride,src.alpha); }

/// Returns a weak reference to clipped \a image (unsafe if referenced image is freed) [FIXME: shared]
Image clip(const Image& image, int2 origin, int2 size);

/// Crops the image without any copy
Image crop(Image&& image, int2 origin, int2 size);

/// Flip the image around the horizontal axis in place
Image flip(Image&& image);

/// Returns a copy of the image resized to \a width x \a height
Image doResize(const Image& image, uint width, uint height);
inline Image resize(const Image& image, uint width, uint height) {
    if(width==image.width && height==image.height) return copy(image);
    else return doResize(image, width, height);
}
inline Image resize(const Image& image, int2 size) { return resize(image, size.x, size.y); }

inline Image resize(Image&& image, uint width, uint height) {
    if(width==image.width && height==image.height) return move(image);
    else return doResize(image, width, height);
}

/// Upsamples an image by duplicating samples
Image upsample(const Image& source);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declares a function lazily decoding an image embedded using FILE
#define ICON(name) \
static const Image& name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
    static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end)); \
    return icon; \
}
