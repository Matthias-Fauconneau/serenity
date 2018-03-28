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
 ImageT(buffer<T>&& pixels, uint2 size, uint stride=0, bool alpha=false) :
     buffer<T>(::move(pixels)), size(size), stride(stride?:size.x), alpha(alpha) {
     assert_(ref<T>::size >= (size.y-1)*this->stride+size.x, ref<T>::size, size.y*this->stride);
 }
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
generic ImageT<T> cropShare(const ImageT<T>& o, int2 offset, uint2 size) {
 //assert_(int2(size) >= int2(0) && offset+int2(size) <= int2(o.size), offset, size, o.size);
 return ImageT<T>(unsafeRef(o.slice(offset.y*o.stride+offset.x, size.y*o.stride-offset.x)), size, o.stride, o.alpha);
}

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

generic ImageT<T> operator*(const ImageT<T>& a, const ImageT<T>& b) {
    ImageT<T> y(a.size);
    for(uint i: range(a.ref::size)) y[i]=a[i]*b[i];
    return y;
}
generic ImageT<T> operator/(const ImageT<T>& a, const ImageT<T>& b) {
    ImageT<T> y(a.size);
    for(uint i: range(a.ref::size)) y[i]=a[i]/b[i];
    return y;
}
inline void opGt(const ImageF& Y, const ImageF& X, float threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]>threshold); }
inline ImageF operator>(const ImageF& X, float threshold) { ImageF Y(X.size); ::opGt(Y,X,threshold); return Y; }


// -- sRGB --

extern uint8 sRGB_forward[0x1000];
extern float sRGB_reverse[0x100];

uint8 sRGB(float v);
inline byte3 sRGB(bgr3f v) { return byte3(sRGB(v.b),sRGB(v.g),sRGB(v.r)); }

/// Converts linear float pixels for each component to color sRGB pixels
void sRGB(const Image& target, const ImageF& source, float max=-inff);
inline Image sRGB(const ImageF& source, const float max=-inff) { Image target(source.size); ::sRGB(target, source, max); return target; }

void sRGB(const Image& target, const Image3f& source, bgr3f max=bgr3f(-inff), const bgr3f min=bgr3f(inff));
inline Image sRGB(const Image3f& source, const bgr3f max=bgr3f(-inff), const bgr3f min=bgr3f(inff)) {
    Image target(source.size); ::sRGB(target, source, max, min); return target;
}

Image3f linear(const Image& source);
ImageF luminance(const Image& source);

// -- Decode --

/// Decodes \a file to an Image
Image decodeImage(const ref<byte> file);

// -- Rotate --

generic void rotateHalfTurn(const ImageT<T>& target);

// -- Resample --

generic void downsample(const ImageT<T>& target, const ImageT<T>& source);
generic ImageT<T> downsample(const ImageT<T>& source) { ImageT<T> target(source.size/2u); downsample(target, source); return target; }

/// Upsamples an image by duplicating samples
generic void upsample(const ImageT<T>& target, const ImageT<T>& source);
generic ImageT<T> upsample(const ImageT<T>& source) { ImageT<T> target(source.size*2u); ::upsample(target, source); return target; }

/// Applies a gaussian blur
void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius=0);
inline ImageF gaussianBlur(ImageF&& target, const ImageF& source, float sigma) { gaussianBlur(target, source, sigma); return move(target); }
inline ImageF gaussianBlur(const ImageF& source, float sigma) { return gaussianBlur(source.size, source, sigma); }
