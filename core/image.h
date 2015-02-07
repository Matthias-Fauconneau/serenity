#pragma once
/// \file image.h Image container and operations
#include "vector.h"
#include "simd.h"

/// 2D array of BGRA 8-bit unsigned integer pixels
struct Image : buffer<byte4> {
	union { int2 size = 0; struct { uint width, height; }; };
    uint stride = 0;
    bool alpha = false, sRGB = true;

	Image() {}
	default_move(Image);
    Image(buffer<byte4>&& pixels, int2 size, uint stride=0, bool alpha=false, bool sRGB=true)
        : buffer<byte4>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha), sRGB(sRGB) {
        //assert_(buffer::data && buffer::size == height*this->stride);
    }
    Image(uint width, uint height, bool alpha=false, bool sRGB=true)
		: buffer(height*width), width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        //assert_(width && height && buffer::data);
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return data && width && height; }
	inline notrace byte4& operator()(uint x, uint y) const { assert(x<width && y<height); return at(y*stride+x); }
};
inline String str(const Image& o) { return strx(o.size); }

inline Image copy(const Image& o) { return Image(copyRef(o), o.size, o.stride, o.alpha, o.sRGB); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline notrace Image share(const Image& o) { return Image(unsafeRef(o),o.size,o.stride,o.alpha,o.sRGB); }

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
inline notrace Image cropShare(const Image& o, int2 offset, int2 size) { return Image(unsafeRef(o.slice(offset.y*o.stride+offset.x, size.y*o.stride)),size,o.stride,o.alpha,o.sRGB); }

// -- Decode --

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
	static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end - _binary_## name ##_start)); \
    return share(icon); \
}

// -- Rotate --

/// Rotates an image
void rotate(const Image& target, const Image& source);
inline Image rotate(Image&& target, const Image& source) { rotate(target, source); return move(target); }
inline Image rotate(const Image& source) { return rotate(Image(int2(source.size.y, source.size.x), source.alpha), source); }
/// Rotates an image around
void rotateHalfTurn(const Image& target);

// -- Resample (3x8bit) --

/// Resizes \a source into \a target
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }

// -- 16bit

struct Image16 : buffer<int16> {
	int2 size = 0;
	Image16() {}
	Image16(ref<int16> ref, int2 size) : buffer(unsafeRef(ref)), size(size) { assert_(Ref::size == (size_t)size.y*size.x); }
	Image16(int2 size) : buffer((size_t)size.y*size.x), size(size) {}
	inline notrace int16& operator()(size_t x, size_t y) const { assert(x<size_t(size.x) && y<size_t(size.y)); return at(y*size.x+x); }
};

// -- 4x float

/// 2D array of floating-point 4 component vector pixels
struct ImageF : buffer<v4sf> {
	ImageF(){}
	ImageF(buffer<v4sf>&& data, int2 size, size_t stride) : buffer(::move(data)), size(size), stride(stride) {
		assert(buffer::size==size_t(size.y*stride), buffer::size, size, stride);
	}
	ImageF(int width, int height) : buffer(height*width), width(width), height(height), stride(width) {
		assert(size>int2(0), size, width, height);
	}
	ImageF(int2 size) : ImageF(size.x, size.y) {}

	inline v4sf& operator()(size_t x, size_t y) const {assert(x<width && y<height, x, y); return at(y*stride+x); }

	union {
		int2 size = 0;
		struct { uint width, height; };
	};
	size_t stride = 0;
};
inline ImageF copy(const ImageF& o) {
	if(o.width == o.stride) return ImageF(copy((const buffer<v4sf>&)o), o.size, o.stride);
	ImageF target(o.size);
	for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
	return target;
}

void box(const ImageF& target, const ImageF& source, const int width/*, const v4sf border*/);
