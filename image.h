#pragma once
#include "array.h"
#include "vector.h"
#include "debug.h"

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,uint,4> int4;

struct Image {
    const byte4* data=0;
    uint width=0, height=0, stride=0;
    bool own=false, alpha=false;

    no_copy(Image)
    Image(Image&& o) : data(o.data), width(o.width), height(o.height), stride(o.stride), own(o.own), alpha(o.alpha) { o.data=0; }
    Image& operator =(Image&& o) {  this->~Image(); data=o.data; o.data=0;
        width=o.width; height=o.height; stride=o.stride;  own=o.own; alpha=o.alpha; return *this; }

    Image(){}
    Image(byte4* data, int width, int height, int stride, bool own, bool alpha) :
        data(data),width(width),height(height),stride(stride),own(own),alpha(alpha){}
    Image(int width, int height, bool alpha=false, int stride=0)
        : data(allocate<byte4>(height*(stride?:width))), width(width), height(height), stride(stride?:width), own(true), alpha(alpha) {
        assert(width); assert(height);
    }
    Image(array<byte4>&& o, uint width, uint height, bool alpha) : data(o.data()),width(width),height(height),stride(width),own(true),alpha(alpha) {
        assert(width && height && o.size() == width*height, width, height, o.size()); assert(o.tag==-2); o.tag = 0;
    }

    ~Image(){ if(data && own) { unallocate(data,height*stride); } }
    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(byte4)); }

    byte4 operator()(uint x, uint y) const {assert(x<width && y<height,int(x),int(y),width,height); return data[y*stride+x]; }
    byte4& operator()(uint x, uint y) {assert(x<width && y<height,int(x),int(y),width,height); return (byte4&)data[y*stride+x]; }
    int2 size() const { return int2(width,height); }
};

inline string str(const Image& o) { return str(o.width,"x"_,o.height); }
/// Creates a new handle to \a image data (unsafe if freed)
inline Image share(const Image& o) { return Image((byte4*)o.data,o.width,o.height,o.stride,false,o.alpha); }
/// Copies the image buffer
inline Image copy(const Image& o) {Image r(o.width,o.height,o.alpha); ::copy((byte4*)r.data,o.data,o.stride*o.height); return r;}
/// Returns a copy of the image resized to \a width x \a height
Image resize(const Image& image, uint width, uint height);
/// Flip the image around the horizontal axis in place
inline Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++)  swap(image(x,y),image(x,h-1-y));
    return move(image);
}
/// Decodes \a file to an Image
Image decodeImage(const ref<byte>& file);

/// Declare a small .png icon embedded in the binary, accessible at runtime as an Image (lazily decoded)
/// \note an icon with the same name must be linked by the build system
///       'ld -r -b binary -o name.o name.png' can be used to embed a file in the binary
#define ICON(name) \
static const Image& name ## Icon() { \
    extern byte _binary_icons_## name ##_png_start[]; extern byte _binary_icons_## name ##_png_end[]; \
    static Image icon = decodeImage(array<byte>(_binary_icons_## name ##_png_start, _binary_icons_## name ##_png_end-_binary_icons_## name ##_png_start)); \
    return icon; \
}

/// Gamma correction lookup table
constexpr uint8 gamma[257] = { 0, 12, 21, 28, 33, 38, 42, 46, 49, 52, 55, 58, 61, 63, 66, 68, 70, 73, 75, 77, 79, 81, 82, 84, 86, 88, 89, 91, 93, 94, 96, 97, 99, 100, 102, 103, 104, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126, 127, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160, 161, 161, 162, 163, 164, 165, 165, 166, 167, 168, 168, 169, 170, 171, 171, 172, 173, 174, 174, 175, 176, 176, 177, 178, 179, 179, 180, 181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193, 193, 194, 194, 195, 196, 196, 197, 197, 198, 199, 199, 200, 201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206, 207, 208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 222, 223, 223, 224, 224, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 237, 238, 238, 239, 239, 240, 240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 245, 246, 246, 247, 247, 248, 248, 249, 249, 250, 250, 251, 251, 251, 252, 252, 253, 253, 254, 254, 254, 255 };
#if 0
#define pow __builtin_pow
inline float sRGB(float c) { if(c>=0.0031308f) return 1.055f*pow(c,1/2.4f)-0.055f; else return 12.92f*c; }
int main() {  for(int i=0;i<=256;i++) write(1,string(str(min(255,int(255*sRGB(i/255.f))))+", "_));  }
#endif
