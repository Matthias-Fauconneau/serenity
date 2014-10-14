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
    uint stride = 0;
    bool alpha = false, sRGB = true;

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

inline Image copy(const Image& o) { return Image(copy((const buffer<byte4>&)o),o.size,o.stride,o.alpha,o.sRGB); }

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
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }

// -- ImageF --

/// 2D array of floating-point pixels
struct ImageF : buffer<float> {
    ImageF(){}
    ImageF(buffer<float>&& data, int2 size, uint stride) : buffer(::move(data)), size(size), stride(stride) {
        assert_(buffer::size==size_t(size.y*stride), buffer::size, size, stride);
    }
    ImageF(int width, int height) : buffer(height*width), width(width), height(height), stride(width) { assert_(size>int2(0), size, width, height); }
    ImageF(int2 size) : ImageF(size.x, size.y) {}

    explicit operator bool() const { return data && width && height; }
    inline float& operator()(uint x, uint y) const {assert(x<width && y<height, x, y); return at(y*stride+x); }
    inline float& operator()(int2 p) const { return operator()(p.x, p.y); }

    union {
        int2 size = 0;
        struct { uint width, height; };
    };
    uint stride = 0;
};

inline ImageF copy(const ImageF& o) { return ImageF(copy((const buffer<float>&)o), o.size, o.stride); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline ImageF share(const ImageF& o) { return ImageF(unsafeReference(o), o.size, o.stride); }

/// Returns a cropped weak reference to \a image (unsafe if referenced image is freed)
inline ImageF crop(const ImageF& source, int2 origin, int2 size) {
    origin = clip(int2(0), origin, source.size);
    size = min(size, source.size-origin);
    return ImageF(buffer<float>(source.begin()+origin.y*source.stride+origin.x, size.y*source.stride), size, source.stride);
}

//inline ImageF operator/(ImageF&& a, const ImageF& b) { assert_(a.stride==b.stride); parallel::div(a, a, b); return move(a); }

template<Type F, Type... S> void apply(const ImageF& target, F function, const S&... sources) {
    for(int2 size: ref<int2>{sources.size...}) assert_(target.size == size, target.size, sources.size...);
    parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
        for(size_t y: range(start, start+chunkSize)) for(size_t x: range(target.size.x)) {
            target[y*target.stride + x] = function(sources[y*sources.stride + x]...);
        }
    });
}
template<Type F> ImageF apply(ImageF&& y, const ImageF& a, const ImageF& b, F function) { apply(y, function, a, b); return move(y); }
template<Type F> ImageF apply(const ImageF& a, const ImageF& b, F function) { return apply(a.size, a, b, function); }

inline void min(const ImageF& y, const ImageF& a, const ImageF& b) { apply(y, [](float a, float b){ return min(a, b);}, a, b); }
inline void max(const ImageF& y, const ImageF& a, const ImageF& b) { apply(y, [](float a, float b){ return max(a, b);}, a, b); }

inline ImageF min(ImageF&& y, const ImageF& a, const ImageF& b) { min(y, a, b); return move(y); }
inline ImageF min(const ImageF& a, const ImageF& b) { return min(a.size, a, b); }

inline ImageF operator-(const ImageF& a, const ImageF& b) { return apply(a, b, [](float a, float b){ return a - b;}); }
inline ImageF operator/(const ImageF& a, const ImageF& b) { return apply(a, b, [](float a, float b){ return a / b;}); }

template<Type F, Type... S> void applyXY(const ImageF& target, F function, const S&... source) {
    parallel_chunk(target.size.y, [&](uint, uint64 start, uint64 chunkSize) {
        for(size_t y: range(start, start+chunkSize)) for(size_t x: range(target.size.x)) {
            target[y*target.stride + x] = function(x, y, source[y*source.stride + x]...);
        }
    });
}

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

/// Selects image (signal) components of scale (frequency) below threshold
/// Applies a gaussian blur
void gaussianBlur(const ImageF& target, const ImageF& source, float sigma);
inline ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma) { gaussianBlur(target, source, sigma); return move(target); }
inline ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }

/// Selects image (signal) components of scale (frequency) above threshold
inline void highPass(const ImageF& target, const ImageF& source, float threshold) {
    parallel::sub(target, source, gaussianBlur(source, threshold));
}

/// Selects image (signal) components of scales (frequencies) within a band
inline void bandPass(const ImageF& target, const ImageF& source, float lowThreshold, float highThreshold) {
    highPass(target, gaussianBlur(source, lowThreshold), highThreshold);
}

// -- Detection --

inline int2 argmin(const ImageF& source) {
    struct C { int2 position=0; float value=inf; };
    C minimums[threadCount];
    mref<C>(minimums).clear(); // Some threads may not iterate
    parallel_chunk(source.size.y, [&](uint id, uint64 start, uint64 chunkSize) {
        C min;
        for(size_t y: range(start, start+chunkSize)) {
            for(size_t x: range(source.size.x)) { float v = source(x,y); if(v < min.value) min = C{int2(x,y), v}; }
        }
        minimums[id] = min;
    });
    C min;
    for(C v: minimums) { if(v.value < min.value) min = v; }
    return min.position;
}
