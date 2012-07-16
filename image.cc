#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include "array.cc"
#include "vector.cc"

#define generic template<class T>

generic Image<T>::Image(array<T>&& data, uint width, uint height)
    : data((T*)data.data()),width(width),height(height),stride(width),own(true) {
    assert(data.size() >= width*height, data.size(), width, height);
    assert(data.buffer.capacity);
    data.buffer.capacity = 0; //taking ownership
}

Image<byte4> resize(const Image<byte4>& image, uint width, uint height) {
    if(!image) return Image<byte4>();
    if(width==image.width && height==image.height) return copy(image);
    Image<byte4> target(width,height);
    const byte4* src = image.data;
    byte4* dst = target.data;
    if(image.width/width==image.height/height && !(image.width%width) && !(image.height%height)) { //integer box
        int scale = image.width/width;
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                int4 s; //TODO: alpha blending
                for(int i=0;i<scale;i++){
                    for(int j=0;j<scale;j++) {
                        s+= int4(src[i*image.width+j]);
                    }
                }
                *dst = byte4(s/(scale*scale));
                src+=scale, dst++;
            }
            src += (scale-1)*image.width;
        }
    } else { //nearest
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                *dst = src[(y*height/image.height)*image.width+x*width/image.width];
                dst++;
            }
        }
    }
    return target;
}

Image<byte4> swap(Image<byte4>&& image) {
    uint32* p = (uint32*)image.data;
    for(uint i=0;i<image.width*image.height;i++) p[i] = swap32(p[i]);
    return move(image);
}

Image<byte4> flip(Image<byte4>&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++) {
        swap(image(x,y),image(x,h-1-y));
    }
    return move(image);
}

weak(Image<byte4> decodePNG(const array<byte>&)) { error("PNG support not linked"); }
weak(Image<byte4> decodeJPEG(const array<byte>&)) { error("JPEG support not linked"); }
weak(Image<byte4> decodeICO(const array<byte>&)) { error("ICO support not linked"); }

Image<byte4> decodeImage(const array<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else { warn("Unknown image format",slice(file,0,4)); return Image<byte4>(); }
}

