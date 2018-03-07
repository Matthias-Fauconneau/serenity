#pragma once
/// \file image.h Image container and operations
#include "vector.h"

/// 2D array of pixels
generic struct ImageT : buffer<T> {
 uint2 size = 0_0;
 uint stride = 0;
 bool alpha = false;

 ImageT() {}
 default_move(ImageT);
 ImageT(buffer<T>&& pixels, uint2 size, uint stride=0, bool alpha=false) : buffer<T>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha) {
     assert_(ref<T>::size == size.y*this->stride, ref<T>::size, size.y*this->stride); }
 ImageT(uint width, uint height, bool alpha=false) : buffer<T>(height*width), size(width, height), stride(width), alpha(alpha) {}
 ImageT(uint2 size, bool alpha=false) : ImageT(size.x, size.y, alpha) {}

 explicit operator bool() const { return buffer<T>::data && size.x && size.y; }
 inline T& operator()(uint x, uint y) const {
  assert(x<uint(size.x) && y<uint(size.y), int(x), int(y), size);
  return buffer<T>::at(y*stride+x);
 }
 inline T& operator()(uint2 v) const { return operator()(v.x, v.y); }
};

generic void clear(const ImageT<T>& target, T v) {
 for(size_t y: range(target.size.y)) target.slice(y*target.stride, target.size.x).clear(v);
}


generic ImageT<T> copy(const ImageT<T>& o) {
 if(o.width == o.stride) return ImageT<T>(copyRef(o), o.size, o.stride, o.alpha);
 ImageT<T> target(o.size, o.alpha);
 for(size_t y: range(o.height)) target.slice(y*target.stride, target.width).copy(o.slice(y*o.stride, o.width));
 return target;
}
generic void copy(const ImageT<T>& target, const ImageT<T>& o) {
 for(size_t y: range(target.size.y)) target.slice(y*target.stride, target.size.x).copy(o.slice(y*o.stride, o.size.x));
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
generic ImageT<T> unsafeShare(const ImageT<T>& o) {
 return ImageT<T>(unsafeRef(o),o.size,o.stride,o.alpha);
}

/// Returns a weak reference to \a image (unsafe if referenced image is freed)
/*generic ImageT<T> cropShare(const ImageT<T>& o, int2 offset, uint2 size) {
 assert_(int2(size) >= int2(0) && offset+int2(size) <= int2(o.size), offset, size, o.size);
 return ImageT<T>(unsafeRef(o.slice(offset.y*o.stride+offset.x, size.y*o.stride)), size, o.stride, o.alpha);
}*/

// -- Types --

/// 2D array of BGRA 8-bit unsigned integer pixels (sRGB colorspace)
typedef ImageT<byte4> Image;

/// 2D array of 32bit floating-point samples
typedef ImageT<float> ImageF;

/// 2D array of 16bit floating-point samples
typedef ImageT<half> ImageH;

/// 2D array of 8bit integer samples
typedef ImageT<uint8> Image8;

/// 2D array of 16bit integer samples
typedef ImageT<uint16> Image16;

/// 2D array of 32bit integer samples
typedef ImageT<uint32> Image32;

/// 2D array of RGB 32bit floating-point samples
typedef ImageT<bgr3f> Image3f;

/// 2D array of BGRA 32bit floating-point samples
typedef ImageT<bgra4f> Image4f;

// -- sRGB --

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

uint8 sRGB(float v);

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(const Image& target, const Image3f& source);
inline Image sRGB(const Image3f& source) { Image target(source.size); ::sRGB(target, source); return target; }

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(const Image& target, const Image4f& source);
inline Image sRGB(const Image4f& source) { Image target(source.size); ::sRGB(target, source); return target; }

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(const Image& BGR, const ImageF& B, const ImageF& G, const ImageF& R);
inline Image sRGB(const ImageF& B, const ImageF& G, const ImageF& R) { Image target(B.size); ::sRGB(target, B, G, R); return target; }

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(const Image& BGR, const Image16& N, const ImageF& B, const ImageF& G, const ImageF& R);
inline Image sRGB(const Image16& N, const ImageF& B, const ImageF& G, const ImageF& R) { Image target(N.size); ::sRGB(target, N, B, G, R); return target; }

void sRGB(const Image& BGR, const Image8& M);
void sRGB(const Image& BGR, const Image16& N);
void sRGB(const Image& BGR, const ImageF& Z);

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

generic void flip(const ImageT<T>& image);
void flip(const Image& target, const Image& source);
Image flip(Image&& image);

void negate(const Image& target, const Image& source);
inline Image negate(const Image& source) { Image target(source.size); ::negate(target, source); return target; }

// -- Resample (3x8bit) --

/// Upsamples an image by duplicating samples
void upsample(const Image& target, const Image& source);
inline Image upsample(const Image& source) { Image target(source.size*2u); ::upsample(target, source); return target; }

/// Resizes \a source into \a target
void resize(const Image& target, const Image& source);
inline Image resize(Image&& target, const Image& source) { resize(target, source); return move(target); }
inline Image resize(uint2 size, const Image& source) { return resize(Image(size, source.alpha), source); }
