#pragma once
/// \file image.h Image container and operations
#include "vector.h"

inline String strx(int2 N) { return str(N.x)+"x"_+str(N.y); }
inline String strx(int3 N) { return str(N.x)+"x"_+str(N.y)+"x"_+str(N.z); }

/// Axis-aligned rectangle
struct Rect {
    int2 min,max;
    Rect(int2 min, int2 max):min(min),max(max){}
    explicit Rect(int2 max):min(0,0),max(max){}
    bool contains(int2 p) { return p>=min && p<max; }
    int2 position() { return min; }
    int2 size() { return max-min; }
};
inline bool operator ==(const Rect& a, const Rect& b) { return a.min==b.min && a.max==b.max; }
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline String str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

struct Image {
    Image(){}
    Image(::buffer<byte4>&& buffer, byte4* data, uint width, uint height, uint stride, bool alpha=false, bool sRGB=false) :
        buffer(move(buffer)),data(data),width(width),height(height),stride(stride),alpha(alpha),sRGB(sRGB){}
    Image(uint width, uint height, bool alpha=false, bool sRGB=true) : width(width), height(height), stride(width), alpha(alpha), sRGB(sRGB) {
        assert(width); assert(height);
        buffer=::buffer<byte4>(height*(stride?:width)); data=buffer.begin();
    }
    Image(int2 size) : Image(size.x, size.y) {}

    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }
    int2 size() const { return int2(width,height); }
    inline byte4& operator()(uint x, uint y) const {assert(x<width && y<height); return data[y*stride+x]; }

    ::buffer<byte4> buffer; //FIXME: shared
    byte4* data=0; // First pixel
    uint width=0, height=0, stride=0;
    bool alpha=false, sRGB=true;
};
inline String str(const Image& o) { return str(o.width,"x"_,o.height); }

/// Copies an image
inline void copy(const Image& target, const Image& source) {
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

/// Upsamples an image by duplicating samples
Image upsample(const Image& source);

/// Returns the image file format if valid
string imageFileFormat(const ref<byte>& file);

/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declares a function lazily decoding an embedded icon
#define ICON(name) \
static const Image& name ## Icon() { \
    extern byte _binary_## name ##_start[]; extern byte _binary_## name ##_end[]; \
    static Image icon = decodeImage(ref<byte>(_binary_## name ##_start, _binary_## name ##_end)); \
    return icon; \
}

struct ImageF {
    ImageF(){}
    ImageF(buffer<float>&& data, int2 size) : data(move(data)), size(size) { assert_(this->data.size==size_t(size.x*size.y)); }
    ImageF(int2 size) : size(size) { assert(size); data=::buffer<float>(size.x*size.y); }
    inline float& operator()(uint x, uint y) const {assert(x<uint(size.x) && y<uint(size.y)); return data[y*size.x+x]; }
    buffer<float> data;
    int2 size;
};
inline ImageF share(const ImageF& o) { return ImageF(unsafeReference(o.data),o.size); }
//inline ImageF operator*(float scale, ImageF&& image) { for(float& v: image.data) v *= scale; return move(image); }

/// Converts a linear float image to sRGB
float convert(const Image& target, const ImageF& source, float max=0);
/// Downsamples by adding samples
void downsample(const ImageF& target, const ImageF& source);
/// Upsamples an image by duplicating samples
ImageF upsample(const ImageF& source);
