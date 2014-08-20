#include "image.h"
#include "data.h"
#include "vector.h"

Image clip(const Image& image, Rect r) {
    r = r & Rect(image.size());
    return Image(unsafeReference(image.buffer),
                 image.data+r.position().y*image.stride+r.position().x, r.size().x, r.size().y, image.stride, image.alpha, image.sRGB);
}

Image transpose(const Image& source) {
    int w=source.width, h=source.height;
    Image target(h,w);
    for(int y: range(h)) for(int x: range(w)) target(y, x) = source(x,y);
    return target;
}

Image rotate(const Image& source) {
    int w=source.width, h=source.height;
    Image target(h,w);
    for(int y: range(h)) for(int x: range(w)) target(y, w-x-1) = source(x,y);
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

void downsample(const Image& target, const Image& source) {
    int w=source.width, h=source.height;
    //assert_(w%2==0 && h%2==0, w, h);
    // Averages values as if in linear space (not sRGB)
    for(uint y: range(h/2)) for(uint x: range(w/2)) target(x,y) = byte4((int4(source(x*2+0,y*2+0)) + int4(source(x*2+1,y*2+0)) +
                                                                   int4(source(x*2+0,y*2+1)) + int4(source(x*2+1,y*2+1)) + int4(2)) / 4);
}

Image downsample(const Image& source) {
    Image target(source.size()/2);
    downsample(target, source);
    return target;
}

Image resize(Image&& target, const Image& source) {
    assert_(source && target && target.size() != source.size() && source.alpha==false && target.alpha==false, source.size(), target.size());
    // Integer box downsample
    assert_(source.width/target.width==source.height/target.height && source.width%target.width==0 && source.height%target.height==0,
            source.size(), target.size());
    byte4* dst = target.data;
    const byte4* src = source.data;
    int scale = source.width/target.width;
    for(uint unused y: range(target.height)) {
        const byte4* line = src;
        for(uint unused x: range(target.width)) {
            int4 s = 0;
            for(uint i: range(scale)) {
                for(uint j: range(scale)) {
                    s += int4(line[i*source.stride+j]);
                }
            }
            s /= scale*scale;
            *dst = byte4(s);
            line += scale, dst++;
        }
        src += scale * source.stride;
    }
    return move(target);
}


string imageFileFormat(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
    else if(startsWith(file,"\x89PNG\r\n\x1A\n"_)) return "PNG"_;
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
    else if(startsWith(file,"BM"_)) return "BMP"_;
    else return ""_;
}

int2 imageSize(const ref<byte>& file) {
    BinaryData s(file, true);
    if(s.read<byte>(8)!="\x89PNG\r\n\x1A\n"_) {
        for(;;) {
            s.advance(4);
            if(s.read<byte>(4) == "IHDR"_) {
                uint width = s.read(), height = s.read();
                return int2(width, height);
            }
        }
    }
    else return {};
}

Image  __attribute((weak)) decodePNG(const ref<byte>&) { error("PNG support not linked"_); }
Image  __attribute((weak)) decodeJPEG(const ref<byte>&) { error("JPEG support not linked"_); }
Image  __attribute((weak)) decodeICO(const ref<byte>&) { error("ICO support not linked"_); }
Image  __attribute((weak)) decodeTIFF(const ref<byte>&) { error("TIFF support not linked"_); }
Image  __attribute((weak)) decodeBMP(const ref<byte>&) { error("BMP support not linked"_); }
Image  __attribute((weak)) decodeTGA(const ref<byte>&) { error("TGA support not linked"_); }

Image decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else if(startsWith(file,"\x00\x00\x02\x00"_)||startsWith(file,"\x00\x00\x0A\x00"_)) return decodeTGA(file);
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
    else if(startsWith(file,"BM"_)) return decodeBMP(file);
    else { if(file.size) error("Unknown image format"_,hex(file.slice(0,min<int>(file.size,4)))); return Image(); }
}
