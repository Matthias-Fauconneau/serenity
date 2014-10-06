#include "image.h"
#include "data.h"
#include "vector.h"
#include "parallel.h"

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
    //assert_(source.width*target.height==source.height*target.width, source.size, target.size); // Restricts to exact ratios
    assert_(source.width/target.width==source.height/target.height, source.size, target.size); // Crops to nearest ratio
    assert_(source.width%target.width<=source.width/target.width && source.height%target.height<=source.height/target.height);
    assert_(!source.alpha); //FIXME: not alpha correct
    uint scale = source.width/target.width;
    assert_(scale <= 16);
    chunk_parallel(target.height, [&](uint, uint y) {
        const byte4* sourceLine = source.pixels.data + y * scale * source.stride;
        byte4* targetLine = target.pixels.begin() + y * target.stride;
        for(uint unused x: range(target.width)) {
            const byte4* sourceSpanOrigin = sourceLine + x* scale;
            uint4 s = 0;
            for(uint i: range(scale)) {
                const byte4* sourceSpan = sourceSpanOrigin + i * source.stride;
                for(uint j: range(scale)) s += uint4(sourceSpan[j]);
            }
            s /= scale*scale;
            targetLine[x] = byte4(s[0], s[1], s[2], 0xFF);
        }
    });
    return move(target);
}

static Image bilinear(Image&& target, const Image& source) {
    assert_(!source.alpha);
    error("Unused", target.size, source.size, source.width%target.width==0, source.height%target.height==0);
    const uint stride = source.stride*4, width=source.width-1, height=source.height-1;
    const uint targetStride=target.stride, targetWidth=target.width, targetHeight=target.height;
    const uint8* src = (const uint8*)source.pixels.data; byte4* dst (target.pixels);
    for(uint y: range(targetHeight)) {
        for(uint x: range(targetWidth)) {
            const uint fx = x*256*width/targetWidth, fy = y*256*height/targetHeight; //TODO: incremental
            uint ix = fx/256, iy = fy/256;
            uint u = fx%256, v = fy%256;
            const uint8* s = src+iy*stride+ix*4;
            byte4 d;
            for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
                d[i] = ((uint(s[           i]) * (256-u) + uint(s[           4+i]) * u) * (256-v)
                       + (uint(s[stride+i]) * (256-u) + uint(s[stride+4+i]) * u) * (       v) ) / (256*256);
            }
            d[3] = 0xFF;
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

static void linear(mref<float> target, ref<byte4> source, Component component) { /**/  if(component==Blue)
        parallel_apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB.b]; }, source);
    else if(component==Green)
        parallel_apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB.g]; }, source);
    else if(component==Red)
        parallel_apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB.r]; }, source);
    else if(component==Mean)
        parallel_apply(target, [](byte4 sRGB) { return (sRGB_reverse[sRGB.b]+sRGB_reverse[sRGB.g]+sRGB_reverse[sRGB.r])/3; }, source);
    else error(component);
    assert_(max(target), component);
}
ImageF linear(ImageF&& target, const Image& source, Component component) {
    linear(target.pixels, source.pixels, component);
    return move(target);
}

static ImageF downsample(ImageF&& target, const ImageF& source) {
    assert_(target.size == source.size/2, target.size, source.size);
    for(uint y: range(target.height)) for(uint x: range(target.width))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
    return move(target);
}

static bool isPowerOfTwo(uint v) { return !(v & (v - 1)); }
static uint log2(uint v) { uint r=0; while(v >>= 1) r++; return r; }
//ImageF resize(ImageF&& target, const ImageF& source) {
ImageF resize(ImageF&& target, ImageF&& source) {
    assert_(source.width*target.height==source.height*target.width); // Uniform scale
    assert_(source.size > target.size, target.size, source.size); // Downsample
    assert_(source.size.x%target.size.x == 0, target.size, source.size); // Integer ratio
    assert_(isPowerOfTwo(source.size.x/target.size.x)); // Mipmap downsample
    int times = log2(uint(source.size.x/target.size.x));
    ImageF inplaceSource = share(source);
    for(uint unused iteration: range(times-1)) {
        ImageF inplaceTarget = share(inplaceSource); inplaceTarget.size = inplaceSource.size/2;
        inplaceTarget = downsample(move(inplaceTarget), inplaceSource);
        inplaceSource = move(inplaceTarget);
    }
    return downsample(move(target), inplaceSource);
}

static uint8 sRGB(float v) {
    v = ::min(1.f, v); // Saturates
    v = ::max(0.f, v); // Clips
    //assert_(v>=0, v);
    uint linear12 = 0xFFF*v;
    assert_(linear12 < 0x1000);
    return sRGB_forward[linear12];
}

static void sRGB(mref<byte4> target, ref<float> source) { apply(target, source, [](float s) { uint8 v = sRGB(s); return byte4(v, v, v, 0xFF); }); }
Image sRGB(Image&& target, const ImageF& source) {
    sRGB(target.pixels, source.pixels);
    return move(target);
}

static byte4 sRGB(float b, float g, float r) {  return byte4(sRGB(b), sRGB(g), sRGB(r), 0xFF); }

static void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red) {
    apply(target, [&](uint index) { return sRGB(blue[index], green[index], red[index]); });
}
Image sRGB(Image&& target, const ImageF& blue, const ImageF& green, const ImageF& red) {
    sRGB(target.pixels, blue.pixels, green.pixels, red.pixels);
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
