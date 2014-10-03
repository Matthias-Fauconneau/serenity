#pragma once
/// \file image.h Image container and operations
#include "vector.h"

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

struct Image {
    Image():size(0){}
    Image(::buffer<byte4>&& buffer, byte4* data, uint width, uint height, uint stride, bool alpha, bool sRGB) :
        buffer(move(buffer)),data(data),width(width),height(height),stride(stride),alpha(alpha),sRGB(sRGB){}
    Image(uint width, uint height, bool alpha=false, bool sRGB=true) : width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        assert(width); assert(height);
        buffer=::buffer<byte4>(height*(stride?:width)); data=buffer.begin();
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return data && width && height; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }
    inline byte4& operator()(uint x, uint y) const {assert(x<width && y<height); return data[y*stride+x]; }

    ::buffer<byte4> buffer; //FIXME: shared
    byte4* data=0; // First pixel
    union {
        struct { uint width, height; };
        int2 size;
    };
    uint stride=0;
    bool alpha=false, sRGB=true;
};
inline String str(const Image& o) { return str(o.width,"x"_,o.height); }

/// Copies an image
inline void copy(const Image& target, const Image& source) {
    assert_(target.size == source.size, target.size, source.size);
    for(uint y: range(source.height)) for(uint x: range(source.width)) target(x,y) = source(x,y);
}

/// Copies an image
inline Image copy(const Image& source) {
    Image target(source.width, source.height, source.alpha, source.sRGB);
    copy(target, source);
    return target;
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline Image share(const Image& o) { return Image(unsafeReference(o.buffer),o.data,o.width,o.height,o.stride,o.alpha,o.sRGB); }

/// Resizes \a source into \a target
/// \note Only supports integer box downsample
Image resize(Image&& target, const Image& source);

Image negate(Image&& target, const Image& source);

/// Returns the image file format if valid
string imageFileFormat(const ref<byte>& file);

/// Returns the image size
int2 imageSize(const ref<byte>& file);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declares a function lazily decoding an image embedded using FILE
#define ICON(name) \
static Image name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
    static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end)); \
    return share(icon); \
}
