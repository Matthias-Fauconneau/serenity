#pragma once
/// \file image.h Image container and operations
#include "vector.h"
#include "simd.h"

/// 2D array of pixels
generic struct ImageT : buffer<T> {
 uint2 size = 0; //union { int2 size = 0; struct { uint width, height; }; };
 uint stride = 0;
 bool alpha = false;

 ImageT() {}
 default_move(ImageT);
 ImageT(buffer<T>&& pixels, uint2 size, uint stride=0, bool alpha=false) : buffer<T>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha) {
  //assert_(buffer::data && buffer::size == height*this->stride);
 }
 ImageT(uint width, uint height, bool alpha=false) : buffer<T>(height*width), size(width, height), stride(width), alpha(alpha) {
  //assert_(width && height && buffer::data);
 }
 ImageT(int2 size, bool alpha=false) : ImageT(size.x, size.y, alpha) {}

 explicit operator bool() const { return buffer<T>::data && size.x && size.y; }
 inline T& operator()(uint x, uint y) const {
  assert(x<uint(size.x) && y<uint(size.y));
  return buffer<T>::at(y*stride+x);
 }
};
generic ImageT<T> copy(const ImageT<T>& o) {
 if(o.width == o.stride) return ImageT<T>(copyRef(o), o.size, o.stride, o.alpha);
 ImageT<T> target(o.size, o.alpha);
 for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
 return target;
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> unsafeShare(const ImageT<T>& o) {
 return ImageT<T>(unsafeRef(o),o.size,o.stride,o.alpha);
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> cropShare(const ImageT<T>& o, int2 offset, int2 size) {
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
 return unsafeShare(icon); \
 }

// -- Rotate --

void flip(Image& image);
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

/// 2D array of 32bit floating-point pixels
typedef ImageT<float> ImageF;
