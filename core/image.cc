#include "image.h"
#include "data.h"
#include "vector.h"

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
void __attribute((constructor(1001))) generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
        real linear = (real) index / (sizeof(sRGB_forward)-1);
        real sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
        assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))<exp2(-50));
        sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
void __attribute((constructor(1001))) generate_sRGB_reverse() {
    for(uint index: range(0x100)) {
        real sRGB = (real) index / 0xFF;
        real linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
        assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))<exp2(-50));
        sRGB_reverse[index] = linear;
        assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
    }
}

static Image box(Image&& target, const Image& source) {
    assert_(source.width/target.width==source.height/target.height, source.size, target.size);
    assert_(source.width%target.width<=source.width/target.width && source.height%target.height<=source.height/target.height, source.width%target.width, source.height%target.height);
    //assert_(!source.alpha); FIXME: not alpha correct
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

static Image bilinear(Image&& target, const Image& source) {
    const uint stride = source.stride*4, width=source.width-1, height=source.height-1;
    const uint targetStride=target.stride, targetWidth=target.width, targetHeight=target.height;
    const uint8* src = (uint8*)source.data; byte4* dst = target.data;
    for(uint y: range(targetHeight)) {
        for(uint x: range(targetWidth)) {
            const uint fx = x*256*width/targetWidth, fy = y*256*height/targetHeight; //TODO: incremental
            uint ix = fx/256, iy = fy/256;
            uint u = fx%256, v = fy%256;
            const uint8* s = src+iy*stride+ix*4;
            byte4 d;
            for(int i=0; i<4; i++) { // Interpolates values as if in linear space (not sRGB)
                d[i] = ((uint(s[           i]) * (256-u) + uint(s[           4+i]) * u) * (256-v)
                       + (uint(s[stride+i]) * (256-u) + uint(s[stride+4+i]) * u) * (       v) ) / (256*256);
            }
            dst[y*targetStride+x] = d;
        }
    }
    return move(target);
}

Image resize(Image&& target, const Image& source) {
    assert_(source && target && target.size != source.size, source.size, target.size);
    if(source.width%target.width==0 && source.height%target.height==0) return box(move(target), source); // Integer box downsample
    else if(target.size > source.size/2) return bilinear(move(target), source); // Bilinear resample
    else return bilinear(move(target), box(source.size/(source.size/target.size), source)); // Integer box downsample + Bilinear resample
}

Image negate(Image&& target, const Image& source) {
    assert_(source.sRGB);
    for(uint y: range(target.height)) for(uint x: range(target.width)) {
        byte4 BGRA = source(x, y);
        vec3 linear = source.sRGB ? vec3(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]) : vec3(BGRA.bgr())/float(0xFF);
        int3 negate = int3(round((float(0xFFF)*(vec3(1)-linear))));
        target(x,y) = byte4(sRGB_forward[negate[0]], sRGB_forward[negate[1]], sRGB_forward[negate[2]], BGRA.a);
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
