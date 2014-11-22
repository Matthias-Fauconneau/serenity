#pragma once
/// \file image.h Image container and operations
#include "vector.h"
#include "data.h"
#include "parallel.h"

/// 2D array of BGRA 8-bit unsigned integer pixels
struct Image : buffer<byte4> {
	union { int2 size = 0; struct { uint width, height; }; };
    uint stride = 0;
    bool alpha = false, sRGB = true;

	Image() {}
	default_move(Image);
    Image(buffer<byte4>&& pixels, int2 size, uint stride=0, bool alpha=false, bool sRGB=true)
        : buffer<byte4>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha), sRGB(sRGB) {
		assert_(buffer::data && buffer::size == height*this->stride);
    }
    Image(uint width, uint height, bool alpha=false, bool sRGB=true)
		: buffer(height*width), width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
		assert_(width && height && buffer::data);
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return data && width && height; }
	inline notrace byte4& operator()(uint x, uint y) const { assert(x<width && y<height); return at(y*stride+x); }
};
inline String str(const Image& o) { return strx(o.size); }

inline Image copy(const Image& o) { return Image(copy((const buffer<byte4>&)o),o.size,o.stride,o.alpha,o.sRGB); }

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
/// Rotates an image around
void rotate(const Image& target);

// -- Resample (3x8bit) --

/// Resizes \a source into \a target
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }
