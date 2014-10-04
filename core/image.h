#pragma once
/// \file image.h Image container and operations
#include "vector.h"
#include "data.h"

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

/// 2D array of BGRA 8-bit unsigned integer pixels
struct Image {
    buffer<byte4> pixels; //buffer; //FIXME: shared
    //byte4* data=0; // First pixel
    union {
        struct { uint width, height; };
        int2 size;
    };
    uint stride=0;
    bool alpha=false, sRGB=true;

    Image():size(0){}
    Image(buffer<byte4>&& pixels, int2 size, uint stride, bool alpha=false, bool sRGB=true)
        : pixels(move(pixels)), size(size), stride(stride), alpha(alpha), sRGB(sRGB) {
        assert_(this->pixels.size == height*stride, this->pixels.size, size, stride);
    }
    Image(uint width, uint height, bool alpha=false, bool sRGB=true) : width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        assert(width); assert(height);
        pixels = ::buffer<byte4>(height*(stride?:width)); //data=buffer.begin();
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return pixels && width && height; }
    explicit operator ref<byte>() const { assert(width==stride); return cast<byte>(pixels); }
    inline byte4& operator()(uint x, uint y) const { assert(x<width && y<height); return pixels[y*stride+x]; }
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
inline Image share(const Image& o) { return Image(unsafeReference(o.pixels),o.size,o.stride,o.alpha,o.sRGB); }

/// Resizes \a source into \a target
/// \note Only supports integer box downsample
Image resize(Image&& target, const Image& source);

/// 2D array of floating-point pixels
struct ImageF {
    ImageF(){}
    ImageF(buffer<float>&& data, int2 size) : pixels(move(data)), size(size) { assert_(pixels.size==size_t(size.x*size.y)); }
    ImageF(int width, int height) : width(width), height(height) { assert_(size>int2(0)); pixels=::buffer<float>(width*height); }
    ImageF(int2 size) : ImageF(size.x, size.y) {}

    explicit operator bool() const { return pixels && width && height; }
    inline float& operator()(uint x, uint y) const {assert(x<uint(size.x) && y<uint(size.y)); return pixels[y*size.x+x]; }

    buffer<float> pixels;
    union {
        struct { uint width, height; };
        int2 size;
    };
};

/// Converts a linear float image to sRGB
Image sRGB(Image&& target, const ImageF& source);
inline Image sRGB(const ImageF& source) { return sRGB(source.size, source); }

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
