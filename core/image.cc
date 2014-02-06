#include "image.h"
#include "data.h"
#include "vector.h"

Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++) swap(image(x,y),image(x,h-1-y));
    return move(image);
}

Image clip(const Image& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(unsafeReference(image.buffer), image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha, image.sRGB);
}

Image crop(Image&& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(move(image.buffer), image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha, image.sRGB);
}

Image doResize(const Image& image, uint width, uint height) {
    if(!image) return Image();
    assert(width!=image.width || height!=image.height);
    Image target(width, height, image.alpha);
    if(image.width/width==image.height/height && image.width%width<=4 && image.height%height<=4) { //integer box downsample
        byte4* dst = target.data;
        const byte4* src = image.data;
        int scale = image.width/width;
        for(uint unused y: range(height)) {
            const byte4* line = src;
            for(uint unused x: range(width)) {
                int4 s=0; //TODO: alpha blending
                for(uint i: range(scale)) {
                    for(uint j: range(scale)) {
                        s+= int4(line[i*image.stride+j]);
                    }
                }
                s /= scale*scale;
                *dst = byte4(s);
                line+=scale, dst++;
            }
            src += scale*image.stride;
        }
    } else {
        error(image.size(), int2(width,height), int2(image.width%width,image.height%height), image.width/width==image.height/height,
              image.width%width<=4 && image.height%height<=4);
        /*Image mipmap;
        bool needMipmap = image.width>2*width || image.height>2*height;
        if(needMipmap) mipmap = doResize(image, image.width/max(1u,(image.width/width)), image.height/max(1u,image.height/height));
        const Image& source = needMipmap?mipmap:image;
        bilinear(target, source);*/
    }
    return target;
}

Image upsample(const Image& source) {
    int w=source.width, h=source.height;
    Image target(w*2,h*2);
    for(int y=0; y<h; y++) for(int x=0; x<w; x++) {
        target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    }
    return target;
}

Image  __attribute((weak)) decodePNG(const ref<byte>&) { error("PNG support not linked"_); }
Image  __attribute((weak)) decodeJPEG(const ref<byte>&) { error("JPEG support not linked"_); }
Image  __attribute((weak)) decodeICO(const ref<byte>&) { error("ICO support not linked"_); }
Image  __attribute((weak)) decodeTIFF(const ref<byte>&) { error("TIFF support not linked"_); }
Image  __attribute((weak)) decodeBMP(const ref<byte>&) { error("BMP support not linked"_); }
Image  __attribute((weak)) decodeTGA(const ref<byte>&) { error("TGA support not linked"_); }

string imageFileFormat(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
    else if(startsWith(file,"\x89PNG"_)) return "PNG"_;
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
    else if(startsWith(file,"BM"_)) return "BMP"_;
    else return ""_;
}

Image decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else if(startsWith(file,"\x00\x00\x02\x00"_)||startsWith(file,"\x00\x00\x0A\x00"_)) return decodeTGA(file);
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
    else if(startsWith(file,"BM"_)) return decodeBMP(file);
    else { if(file.size) warn("Unknown image format"_,hex(file.slice(0,min<int>(file.size,4)))); return Image(); }
}

uint8 sRGB_lookup[256], inverse_sRGB_lookup[256];
void __attribute((constructor(10000))) compute_sRGB_lookup() {
    for(uint linear: range(256)) {
        float c = linear/255.f;
        uint8 sRGB = round(255*( c>=0.0031308 ? 1.055*pow(c,1/2.4f)-0.055 : 12.92*c ));
        sRGB_lookup[linear] = sRGB, inverse_sRGB_lookup[sRGB] = linear;
    }
}
