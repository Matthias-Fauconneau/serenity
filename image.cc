#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

#include "array.cc"
template class array<byte4>;

Image::Image(array<byte4>&& data, uint width, uint height):data((byte4*)data.data()),width(width),height(height),own(true) {
    assert(data.size() >= width*height, data.size(), width, height);
    data.buffer.capacity = 0; //taking ownership
}

Image resize(const Image& image, uint width, uint height) {
    if(width==image.width && height==image.height) return copy(image);
    assert(image.width/width==image.height/height && !(image.width%width) && !(image.height%height)); //integer uniform downscale
    Image target(width,height);
    const byte4* src = image.data;
    byte4* dst = target.data;
    int scale = image.width/width;
    for(uint y=0; y<height; y++) {
        for(uint x=0; x<width; x++) {
            int4 s=zero;
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
    return target;
}

Image swap(Image&& image) {
    uint32* p = (uint32*)image.data;
    for(uint i=0;i<image.width*image.height;i++) p[i] = swap32(p[i]);
    return move(image);
}

declare(Image decodePNG(const array<byte>&),weak) { error("PNG support not linked"); }
declare(Image decodeJPEG(const array<byte>&),weak) { error("JPEG support not linked"); }

Image decodeImage(const array<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else { log("Unknown image format"); return Image(); }
}
