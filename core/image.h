#pragma once
/// \file image.h Image container and operations
#include "vector.h"

/// 2D array of pixels
generic struct ImageT : buffer<T> {
	union { int2 size = 0; struct { uint width, height; }; };
    uint stride = 0;
    bool alpha = false;

    ImageT() {}
    default_move(ImageT);
    ImageT(buffer<T>&& pixels, int2 size, uint stride=0, bool alpha=false) : buffer<T>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha) {}
    ImageT(uint width, uint height, bool alpha=false) : buffer<T>(height*width), width(width), height(height), stride(width), alpha(alpha) {}
	ImageT(int2 size, bool alpha=false) : ImageT(size.x, size.y, alpha) {}

	explicit operator bool() const { return buffer<T>::data && width && height; }
	inline T& operator()(uint x, uint y) const { assert(x<width && y<height); return buffer<T>::at(y*stride+x); }
    inline mref<T> row(uint y) const { assert(y<height); return buffer<T>::slice(y*stride, width); }
};
generic String str(const ImageT<T>& o) { return strx(o.size); }
generic ImageT<T> copy(const ImageT<T>& o) {
    if(o.width == o.stride) return ImageT<T>(copyRef(o), o.size, o.stride, o.alpha);
    ImageT<T> target(o.size, o.alpha);
    for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
    return target;
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> unsafeRef(const ImageT<T>& o) { return ImageT<T>(unsafeRef((const buffer<T>&)o),o.size,o.stride,o.alpha); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> cropRef(const ImageT<T>& o, int2 offset, int2 size) {
    assert_(offset+size <= o.size, offset, size, o.size);
    return ImageT<T>(unsafeRef(o.slice(offset.y*o.stride+offset.x, size.y*o.stride-offset.x)),size,o.stride,o.alpha);
}

/// 2D array of BGRA 8-bit unsigned integer pixels (sRGB colorspace)
typedef ImageT<byte4> Image;

// -- Decode --

/// Returns the image file format if valid
string imageFileFormat(const ref<byte> file);

/// Returns the image size
int2 imageSize(const ref<byte> file);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte> file);

/// Declares a function lazily decoding an image embedded using FILE
#define ICON(name) Image name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
	static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end - _binary_## name ##_start)); \
    return unsafeRef(icon); \
}

// -- Rotate --

Image flip(Image&& image);
/// Rotates an image
void rotate(const Image& target, const Image& source);
inline Image rotate(Image&& target, const Image& source) { rotate(target, source); return move(target); }
inline Image rotate(const Image& source) { return rotate(Image(int2(source.size.y, source.size.x), source.alpha), source); }
/// Rotates an image around
Image rotateHalfTurn(Image&& target);

// -- Resample (3x8bit) --

/// Resizes \a source into \a target
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }
inline Image resize(int2 size, const Image& source) { return resize(Image(size, source.alpha), source); }

/// 2D array of 8bit integer pixels
typedef ImageT<uint8> Image8;
/// 2D array of 32bit floating-point pixels
typedef ImageT<float> ImageF;

void toFloat(mref<float> target, ref<uint8> source);
ImageF toFloat(ImageF&& target, const Image8& source);
ImageF toFloat(const Image8& source);

void downsample(const Image8& target, const Image8& source);
Image8 downsample(const Image8& source);

void mean(const ImageF& target, const ImageF& buffer, const ImageF& source, uint R);
void sRGBfromBT709(const Image& target, const ImageF& Y, const ImageF& U, const ImageF& V);
