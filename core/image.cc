#include "image.h"
#include "data.h"
#include "vector.h"

Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++) swap(image(x,y),image(x,h-1-y));
    return move(image);
}

Image clip(const Image& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(unsafeReference(image.buffer), image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha);
}

Image crop(Image&& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(move(image.buffer), image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha);
}

// SSE bilinear interpolation adapted from http://fastcpp.blogspot.fr/2011/06/bilinear-pixel-interpolation-using-sse.html
#if __SSE4_1__ && 0
#include <smmintrin.h>

// Computes the four pixel weights for x,y
inline __m128i weights(float x, float y) {
    __m128 psXY = _mm_unpacklo_ps(_mm_set_ss(x), _mm_set_ss(y)); // 0 0 y x
    __m128 psXYfloor = _mm_floor_ps(psXY); // use this line for if you have SSE4
    __m128 psXYfrac = _mm_sub_ps(psXY, psXYfloor); // = frac(psXY)
    __m128 psXYfrac1 = _mm_sub_ps(_mm_set1_ps(1), psXYfrac); // ? ? (1-y) (1-x)
    __m128 w_x = _mm_unpacklo_ps(psXYfrac1, psXYfrac);   // ? ?     x (1-x)
    w_x = _mm_movelh_ps(w_x, w_x);      // x (1-x) x (1-x)
    __m128 w_y = _mm_shuffle_ps(psXYfrac1, psXYfrac, _MM_SHUFFLE(1, 1, 1, 1)); // y y (1-y) (1-y)
    __m128i weighti = _mm_cvtps_epi32(_mm_mul_ps( _mm_mul_ps(w_x, w_y), _mm_set1_ps(256))); // w4 w3 w2 w1
    return _mm_packs_epi32(weighti, weighti);
}

// Loads 4 interleaved components pixels and shuffle components together
inline void gather(const uint* p0, uint stride, __m128i& pRG, __m128i& pBA) {
    // Load the data (2 pixels in one load)
    __m128i p12 = _mm_loadl_epi64((const __m128i*)&p0[0 * stride]);
    __m128i p34 = _mm_loadl_epi64((const __m128i*)&p0[1 * stride]);
    // convert RGBA RGBA RGBA RGAB to RRRR GGGG | BBBB AAAA (AoS to SoA)
    __m128i p1234 = _mm_unpacklo_epi8(p12, p34);
    __m128i p34xx = _mm_unpackhi_epi64(p1234, _mm_setzero_si128());
    __m128i p1234_8bit = _mm_unpacklo_epi8(p1234, p34xx);
    pRG = _mm_unpacklo_epi8(p1234_8bit, _mm_setzero_si128());
    pBA = _mm_unpackhi_epi8(p1234_8bit, _mm_setzero_si128());
}

// Multiplies each pixel components with the associated weight and sums
inline uint reduce(__m128i weights, __m128i pRG, __m128i pBA) {
    //outRG = [w1*R1 + w2*R2 | w3*R3 + w4*R4 | w1*G1 + w2*G2 | w3*G3 + w4*G4]
    __m128i outRG = _mm_madd_epi16(pRG, weights);
    //outBA = [w1*B1 + w2*B2 | w3*B3 + w4*B4 | w1*A1 + w2*A2 | w3*A3 + w4*A4]
    __m128i outBA = _mm_madd_epi16(pBA, weights);
    // horizontal add that will produce the output values (in 32bit)
    __m128i out = _mm_hadd_epi32(outRG, outBA);
    out = _mm_srli_epi32(out, 8); // divide by 256
    // convert 32bit->8bit
    out = _mm_packus_epi32(out, _mm_setzero_si128());
    out = _mm_packus_epi16(out, _mm_setzero_si128());
    return _mm_cvtsi128_si32(out);
}

template<uint scale> void unroll(uint* dst, uint dstStride, const uint* src, uint stride, uint width, uint height) {
    __m128i weights[scale*scale]; // weight registers [00,01,10,11, 00,01,10,11]
    for(uint dy: range(scale)) for(uint dx: range(scale)) weights[dy*scale+dx]= ::weights(float(dx)/scale, float(dy)/scale);
    for(uint y: range(height)) for(uint x: range(width)) {
        __m128i pRG, pBA; gather(src+y*stride+x, stride, pRG, pBA);
        for(uint dy: range(scale)) for(uint dx: range(scale))
            dst[(y*scale+dy)*dstStride+x*scale+dx] = reduce(weights[dy*scale+dx], pRG, pBA);
    }
}

void bilinear(Image& target, const Image& source) {
    const uint stride = source.stride, width=source.width-1, height=source.height-1;
    const uint targetStride=target.stride, targetWidth=target.width, targetHeight=target.height;
    const uint* src = (const uint*)source.data; uint* dst = (uint*)target.data;
    if(targetWidth/width==targetHeight/height && targetWidth%width==0 && targetHeight%height==0) { //integer linear upsample
        uint scale = targetWidth/width;
        if(scale==2) { unroll<2>(dst, targetStride, src, stride, width, height); return; }
        if(scale==4) { unroll<4>(dst, targetStride, src, stride, width, height); return; }
        warn("Slow path",scale);
    }
    for(uint y: range(targetHeight)) {
        for(uint x: range(targetWidth)) {
            float srcX = x*width/targetWidth, srcY = y*height/targetHeight; //TODO: incremental
            __m128i pRG, pBA; gather(src+uint(srcY)*stride+uint(srcX), stride, pRG, pBA);
            dst[int(y)*targetStride+int(x)] = reduce(weights(srcX, srcY), pRG, pBA);
        }
    }
}
#else
//TODO: NEON
void bilinear(Image& target, const Image& source) {
    error("bilinear",source.size(),target.size());
    /*const uint stride = source.stride, width=source.width-1, height=source.height-1;
    const uint targetStride=target.stride, targetWidth=target.width, targetHeight=target.height;
    const byte4* src = source.data; byte4* dst = target.data;
    for(uint y: range(targetHeight)) {
        for(uint x: range(targetWidth)) {
            const uint fx = x*256*width/targetWidth, fy = y*256*height/targetHeight; //TODO: incremental
            uint ix = fx/256, iy = fy/256;
            uint u = fx%256, v = fy%256;
            byte* s = (byte*)src+iy*stride+ix*4;
            byte4 d;
            for(int i=0; i<4; i++) {
                d[i] = ((s[       i] * (256-u) + s[       4+i] * u) * (256-v)
                        + (s[stride+i] * (256-u) + s[stride+4+i] * u) * (    v) ) / (256*256);
            }
            dst[y*targetStride+x] = d;
        }
    }*/
}
#endif

Image doResize(const Image& image, uint width, uint height) {
    if(!image) return Image();
    assert(width!=image.width || height!=image.height);
    Image target(width, height, image.alpha);
    if(image.width/width==image.height/height && image.width%width==0 && image.height%height==0) { //integer box downsample
        byte4* dst = target.data;
        const byte4* src = image.data;
        int scale = image.width/width;
        for(uint unused y: range(height)) {
            const byte4* line = src;
            for(uint unused x: range(width)) {
                typedef vector<bgra,int,4> int4;
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
        Image mipmap;
        bool needMipmap = image.width>2*width || image.height>2*height;
        if(needMipmap) mipmap = resize(image, image.width/max(1u,(image.width/width)), image.height/max(1u,image.height/height));
        const Image& source = needMipmap?mipmap:image;
        bilinear(target, source);
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

Image decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
    else if(startsWith(file,"BM"_)) return decodeBMP(file);
    else { if(file.size) log("Unknown image format"_,hex(file.slice(0,min(file.size,4ull)))); return Image(); }
}

uint8 sRGB_lookup[256];
void __attribute((constructor(10000))) compute_sRGB_lookup() {
    for(uint i=0;i<256;i++) {
        float c = i/255.f;
        sRGB_lookup[i] = round(255*( c>=0.0031308 ? 1.055*pow(c,1/2.4f)-0.055 : 12.92*c ));
    }
}
