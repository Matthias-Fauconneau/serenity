#pragma once
/// \file image.h Image container and operations
#include "vector.h"

/// Axis-aligned rectangle
struct Rect {
    int2 min,max;
    Rect(int2 min, int2 max):min(min),max(max){}
    explicit Rect(int2 max):min(0,0),max(max){}
    explicit operator bool() { return min<max; }
    bool contains(int2 p) { return p>=min && p<max; }
    int2 position() { return min; }
    int2 size() { return max-min; }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator &(Rect a, Rect b) { return Rect(min(max(a.min,b.min),b.max),max(min(a.max,b.max),b.min)); }
inline Rect operator |(Rect a, Rect b) { return a && b ? Rect(min(a.min,b.min),max(a.max,b.max)) : (a ? a : b); }
inline Rect& operator |=(Rect& a, Rect b) { return a = a|b; }
inline String str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

struct Image {
    Image(){}
    Image(::buffer<byte4>&& buffer, byte4* data, uint width, uint height, uint stride, bool alpha=false, bool sRGB=false) :
        buffer(move(buffer)),data(data),width(width),height(height),stride(stride),alpha(alpha),sRGB(sRGB){}
    Image(uint width, uint height, bool alpha=false, bool sRGB=true) : width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        assert(width); assert(height);
        buffer=::buffer<byte4>(height*(stride?:width)); data=buffer.begin();
    }
    Image(int2 size, bool alpha=false, bool sRGB=true) : Image(size.x, size.y, alpha, sRGB) {}

    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }
    inline byte4& operator()(uint x, uint y) const {assert(x<width && y<height); return data[y*stride+x]; }
    int2 size() const { return int2(width,height); }

    ::buffer<byte4> buffer; //FIXME: shared
    byte4* data=0; // First pixel
    uint width=0, height=0, stride=0;
    bool alpha=false, sRGB=true;
};
inline String str(const Image& o) { return str(o.width,"x"_,o.height); }

/// Copies an image
inline void copy(const Image& target, const Image& source) {
    assert_(target.size() == source.size(), target.size(), source.size());
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

/// Returns a weak reference to clipped \a image (unsafe if referenced image is freed) [FIXME: shared]
Image clip(const Image& image, Rect region);

/// Downsamples an image by averaging samples
Image downsample(const Image& source);

/// Upsamples an image by duplicating samples
Image upsample(const Image& source);

/// Returns the image file format if valid
string imageFileFormat(const ref<byte>& file);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declares a function lazily decoding an image embedded using FILE
#define ICON(name) \
static const Image& name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
    static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end)); \
    return icon; \
}
