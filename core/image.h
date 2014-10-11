#pragma once
/// \file image.h Image container and operations
#include "vector.h"
#include "data.h"
#include "parallel.h"

/// 2D array of BGRA 8-bit unsigned integer pixels
struct Image : buffer<byte4> {
    union {
        int2 size = 0;
        struct { uint width, height; };
    };
    uint stride=0;
    bool alpha=false, sRGB=true;

    Image():size(0){}
    Image(buffer<byte4>&& pixels, int2 size, uint stride=0, bool alpha=false, bool sRGB=true)
        : buffer<byte4>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha), sRGB(sRGB) {
        assert_(buffer::size == height*this->stride);
    }
    Image(uint width, uint height, bool alpha=false, bool sRGB=true)
        : buffer(height*width), width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        assert(width); assert(height);
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return data && width && height; }
    inline byte4& operator()(uint x, uint y) const { assert(x<width && y<height); return at(y*stride+x); }
};
inline String str(const Image& o) { return strx(o.size); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline Image share(const Image& o) { return Image(unsafeReference(o),o.size,o.stride,o.alpha,o.sRGB); }

// -- Decoding --

/// Returns the image file format if valid
string imageFileFormat(const ref<byte> file);

/// Returns the image size
int2 imageSize(const ref<byte> file);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte> file);

/// Declares a function lazily decoding an image embedded using FILE
#define ICON(name) \
static Image name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
    static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end)); \
    return share(icon); \
}

// -- Resampling (3x8bit) --

/// Resizes \a source into \a target
/// \note Only supports integer box downsample
Image resize(Image&& target, const Image& source);

// -- ImageF --

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
    ImageF(){}
    ImageF(buffer<float>&& data, int2 size) : buffer(::move(data)), size(size) { assert_(buffer::size==size_t(size.x*size.y), size,  width, height, buffer::size); }
    ImageF(int width, int height) : buffer(height*width), width(width), height(height) { assert_(size>int2(0), size, width, height); }
    ImageF(int2 size) : ImageF(size.x, size.y) {}

    explicit operator bool() const { return data && width && height; }
    inline float& operator()(uint x, uint y) const {assert(x<width && y<height, x, y); return at(y*width+x); }

    union {
        int2 size = 0;
        struct { uint width, height; };
    };
};

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline ImageF share(const ImageF& o) { return ImageF(unsafeReference(o),o.size); }

inline ImageF operator-(const ImageF& a, float b) { ImageF y(a.size); subtract(y, a, b); return y; }
//inline ImageF operator-(const ImageF& a, const ImageF& b) { ImageF y(a.size); subtract(y, a, b); return y; }
inline ImageF operator-(const ImageF& a, ImageF&& b) { subtract(b, a, b); return move(b); }
inline ImageF min(ImageF&& a, const ImageF& b) { parallel_apply(a, [](float a, float b) { return min(a, b); }, a, b); return move(a); }

// -- sRGB --

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

/// Converts an sRGB component to linear float
void linear(mref<float> target, ref<byte4> source, uint component);

void sRGB(mref<byte4> target, ref<float> value);
inline Image sRGB(const ImageF& value) {
    Image sRGB (value.size); ::sRGB(sRGB, value); return sRGB;
}

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red);
inline Image sRGB(const ImageF& blue, const ImageF& green, const ImageF& red) {
    Image sRGB (blue.size); ::sRGB(sRGB, blue, green, red); return sRGB;
}

// -- Resampling (float) --

ImageF resize(ImageF&& target, ImageF&& source);

// -- Convolution --

/// Applies a gaussian blur
ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma);
inline ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }

/// Selects image (signal) components of scale (frequency) below threshold
inline ImageF lowPass(const ImageF& source, float threshold) { assert_(threshold>0); return gaussianBlur(source, threshold); }

/// Selects image (signal) components of scale (frequency) above threshold
inline ImageF highPass(const ImageF& source, float threshold) { return source - gaussianBlur(source, threshold); }

/// Selects image (signal) components of scales (frequencies) within a band
inline ImageF bandPass(const ImageF& source, float lowThreshold, float highThreshold) {
    return highPass(lowPass(source, lowThreshold), highThreshold);
}
