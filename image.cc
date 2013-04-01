#include "image.h"
#include "data.h"
#include "vector.h"

Image flip(Image&& image) {
    for(int y=0,h=image.height;y<h/2;y++) for(int x=0,w=image.width;x<w;x++) swap(image(x,y),image(x,h-1-y));
    return move(image);
}

Image clip(const Image& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha);
}

Image crop(Image&& image, int2 origin, int2 size) {
    origin=min(origin,image.size()), size=min(size,image.size()-origin);
    return Image(move(image.buffer), image.data+origin.y*image.stride+origin.x, size.x, size.y, image.stride, image.alpha);
}

#include "smmintrin.h"

// Adapted from http://fastcpp.blogspot.fr/2011/06/bilinear-pixel-interpolation-using-sse.html
template<uint scale> void unroll(byte4* dst, uint dstStride, const byte4* src, uint stride, uint width, uint height) {
    __m128i weights[scale*scale]; // weight registers [00,01,10,11, 00,01,10,11]
    for(uint dy: range(scale)) for(uint dx: range(scale)) {
        __m128 psXYfrac = _mm_unpacklo_ps(_mm_set_ss(float(dx)/scale), _mm_set_ss(float(dy)/scale)); // 0 0 y x
        __m128 psXYfrac1 = _mm_sub_ps(_mm_set1_ps(1), psXYfrac); // ? ? (1-y) (1-x)
        __m128 w_x = _mm_unpacklo_ps(psXYfrac1, psXYfrac);   // ? ?     x (1-x)
        w_x = _mm_movelh_ps(w_x, w_x);      // x (1-x) x (1-x)
        __m128 w_y = _mm_shuffle_ps(psXYfrac1, psXYfrac, _MM_SHUFFLE(1, 1, 1, 1)); // y y (1-y) (1-y)
        __m128 weight = _mm_mul_ps(w_x, w_y); // float weight vector
        weight = _mm_mul_ps(weight, _mm_set1_ps(256));
        __m128i weighti = _mm_cvtps_epi32(weight); // w4 w3 w2 w1
        weighti = _mm_packs_epi32(weighti, weighti); // 32->2x16bit
        weights[dy*scale+dx]= weighti;
    };
    for(uint y: range(height)) for(uint x: range(width)) {
        const byte4* p0 = src+y*stride+x;
        // Load the data (2 pixels in one load)
        __m128i p12 = _mm_loadl_epi64((const __m128i*)&p0[0 * stride]);
        __m128i p34 = _mm_loadl_epi64((const __m128i*)&p0[1 * stride]);
        // convert RGBA RGBA RGBA RGAB to RRRR GGGG BBBB AAAA (AoS to SoA)
        __m128i p1234 = _mm_unpacklo_epi8(p12, p34);
        __m128i p34xx = _mm_unpackhi_epi64(p1234, _mm_setzero_si128());
        __m128i p1234_8bit = _mm_unpacklo_epi8(p1234, p34xx);
        __m128i pRG = _mm_unpacklo_epi8(p1234_8bit, _mm_setzero_si128());
        __m128i pBA = _mm_unpackhi_epi8(p1234_8bit, _mm_setzero_si128());

        for(uint dy: range(scale)) for(uint dx: range(scale)) { //unrolled
                __m128i weight =  weights[dy*scale+dx];
                //outRG = [w1*R1 + w2*R2 | w3*R3 + w4*R4 | w1*G1 + w2*G2 | w3*G3 + w4*G4]
                __m128i outRG = _mm_madd_epi16(pRG, weight);
                //outBA = [w1*B1 + w2*B2 | w3*B3 + w4*B4 | w1*A1 + w2*A2 | w3*A3 + w4*A4]
                __m128i outBA = _mm_madd_epi16(pBA, weight);
                // horizontal add that will produce the output values (in 32bit)
                __m128i out = _mm_hadd_epi32(outRG, outBA);
                out = _mm_srli_epi32(out, 8); // divide by 256
                // convert 32bit->8bit
                out = _mm_packus_epi32(out, _mm_setzero_si128());
                out = _mm_packus_epi16(out, _mm_setzero_si128());
                (uint&)dst[(y*scale+dy)*dstStride+x*scale+dx] = _mm_cvtsi128_si32(out);
        }
    }
}

// Bilinear interpolation (in sRGB space for performance, a correct (linear) version would be target = γcompress( Σ γdecompress( sample ) ) )
void bilinear(Image& target, const Image& source) {
    const byte4* src = source.data;
    const uint stride = source.stride, sourceWidth=source.width-1, sourceHeight=source.height-1;
    uint width=target.width, height=target.height; byte4* dst = target.data; const uint dstStride=target.stride;
    if(width/sourceWidth==height/sourceHeight && width%sourceWidth==0 && height%sourceHeight==0) { //integer linear upsample
        uint scale = width/sourceWidth;
        if(scale==2) unroll<2>(dst, dstStride, src, stride, sourceWidth, sourceHeight);
        else  if(scale==4) unroll<4>(dst, dstStride, src, stride, sourceWidth, sourceHeight);
        else error("FIXME");
    } else {
        error("FIXME");
        for(uint y: range(height)) {
            for(uint x: range(width)) {
                const uint fx = x*256*sourceWidth/width, fy = y*256*sourceHeight/height;
                uint ix = fx/256, iy = fy/256;
                uint u = fx%256, v = fy%256;
                byte* s = (byte*)src+iy*stride+ix*4;
                byte4 d;
                for(int i=0; i<4; i++) {
                    d[i] = ((s[       i] * (256-u) + s[       4+i] * u) * (256-v)
                            + (s[stride+i] * (256-u) + s[stride+4+i] * u) * (    v) ) / (256*256);
                }
                dst[y*dstStride+x] = d;
            }
        }
    }
}

Image resize(const Image& image, uint width, uint height) {
    if(!image) return Image();
    if(width==image.width && height==image.height) return copy(image);
    Image target(width, height, image.alpha);
    if(image.width/width==image.height/height && image.width%width==0 && image.height%height==0) { //integer box downsample
        byte4* dst = (byte4*)target.data;
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
                *dst = byte4(s/(scale*scale));
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

#define weak(function) function __attribute((weak)); function
weak(Image decodePNG(const ref<byte>&)) { error("PNG support not linked"_); }
weak(Image decodeJPEG(const ref<byte>&)) { error("JPEG support not linked"_); }
weak(Image decodeICO(const ref<byte>&)) { error("ICO support not linked"_); }

Image decodeImage(const ref<byte>& file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
    else { if(file.size) log("Unknown image format"_,hex(file.slice(0,min(file.size,4u)))); return Image(); }
}
