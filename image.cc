#include "image.h"
#include "stream.h"
#include "vector.h"
#include "string.h"

Image<byte4> resize(const Image<byte4>& image, uint width, uint height) {
    if(!image) return Image<byte4>();
    if(width==image.width && height==image.height) return copy(image);
    Image<byte4> target(width,height,image.alpha);
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

template<> Image<pixel> convert<pixel,byte4>(const Image<byte4>& source) {
    if(!source) return Image<pixel>();
    Image<pixel> copy(source.width,source.height);
    for(uint x=0;x<source.width;x++) for(uint y=0;y<source.height;y++) {
        if(source.alpha) {
            int4 s=int4(source(x,y));
            copy(x,y)=pixel((s*s.a+int4(255,255,255,255)*(255-s.a))/255);
        } else copy(x,y)=source(x,y);
    }
    return copy;
}

weak(Image<byte4> decodePNG(const ref<byte>&)) { error("PNG support not linked"_); }
weak(Image<byte4> decodeJPEG(const ref<byte>&)) { error("JPEG support not linked"_); }
weak(Image<byte4> decodeICO(const ref<byte>&)) { error("ICO support not linked"_); }

Image<byte4> decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else { warn("Unknown image format"_,hex(file.slice(0,4))); return Image<byte4>(); }
}
