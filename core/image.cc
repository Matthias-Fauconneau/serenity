#include "image.h"
#include "data.h"
#include "vector.h"

Image clip(const Image& image, Rect r) {
    r = r & Rect(image.size());
    return Image(unsafeReference(image.buffer),
                 image.data+r.position().y*image.stride+r.position().x, r.size().x, r.size().y, image.stride, image.alpha, image.sRGB);
}

Image upsample(const Image& source) {
    int w=source.width, h=source.height;
    Image target(w*2,h*2);
    for(int y=0; y<h; y++) for(int x=0; x<w; x++) {
        target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    }
    return target;
}

void convert(const Image& target, const ImageF& source, float max) {
    if(!max) for(uint i: range(source.data.size)) max=::max(max, source.data[i]);
    for(uint y: range(source.height)) for(uint x: range(source.width)) {
        uint linear12 = 0xFFF*clip(0.f, source(x,y)/max, 1.f);
        extern uint8 sRGB_forward[0x1000];
        assert_(linear12 < 0x1000);
        uint8 sRGB = sRGB_forward[linear12];
        target(x,y) = byte4(sRGB, sRGB, sRGB, 0xFF);
    }
}

string imageFileFormat(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
    else if(startsWith(file,"\x89PNG"_)) return "PNG"_;
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
    else if(startsWith(file,"BM"_)) return "BMP"_;
    else return ""_;
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
