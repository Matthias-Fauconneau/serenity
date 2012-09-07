#include "image.h"
#include "stream.h"
#include "vector.h"

Image resize(const Image& image, uint width, uint height) {
    if(!image) return Image();
    if(width==image.width && height==image.height) return copy(image);
    Image target(width,height,image.alpha);
    const byte4* src = image.data;
    byte4* dst = (byte4*)target.data;
    if(image.width/width==image.height/height && !(image.width%width) && !(image.height%height)) { //integer box
        int scale = image.width/width;
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                int4 s=0; //TODO: alpha blending
                for(int i=0;i<scale;i++){
                    for(int j=0;j<scale;j++) {
                        s+= int4(src[i*image.stride+j]);
                    }
                }
                *dst = byte4(s/(scale*scale));
                src+=scale, dst++;
            }
            src += (scale-1)*image.stride;
        }
    } else { //nearest
        for(uint y=0; y<height; y++) {
            for(uint x=0; x<width; x++) {
                *dst = src[(y*image.height/height)*image.stride+x*image.width/width];
                dst++;
            }
        }
    }
    return target;
}

weak(Image decodePNG(const ref<byte>&)) { error("PNG support not linked"_); }
weak(Image decodeJPEG(const ref<byte>&)) { error("JPEG support not linked"_); }
weak(Image decodeICO(const ref<byte>&)) { error("ICO support not linked"_); }

Image decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else { if(file.size) log("Unknown image format"_,hex(file.slice(0,min(file.size,4u)))); return Image(); }
}
