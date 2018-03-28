#include "image.h"
#include "data.h"
#include "vector.h"
#include "math.h"
#include "map.h"
#include "algorithm.h"

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
__attribute((constructor(1001))) static void generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
        double linear = (double) index / (sizeof(sRGB_forward)-1);
        double sRGB = linear > 0.0031308 ? 1.055*__builtin_pow(linear,1/2.4)-0.055 : 12.92*linear;
        assert(abs(linear-(sRGB > 0.04045 ? __builtin_pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))< 0x1p-50);
        sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
__attribute((constructor(1002))) static void generate_sRGB_reverse() {
    for(uint index: range(0x100)) {
        double sRGB = (double) index / 0xFF;
        double linear = sRGB > 0.04045 ? __builtin_pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
        assert(abs(sRGB-(linear > 0.0031308 ? 1.055*__builtin_pow(linear,1/2.4)-0.055 : 12.92*linear))< 0x1p-50);
        sRGB_reverse[index] = linear;
        assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index, sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))], index);
    }
}

uint8 sRGB(float v) {
    v = ::min(1.f, v); // Saturates
    v = ::max(0.f, v); // FIXME
    uint linear12 = 0xFFF*v;
    assert_(linear12 < 0x1000, v);
    return sRGB_forward[linear12];
}

void sRGB(const Image& Y, const ImageF& X, float max) {
    float min = 0;
    if(max <= 0) {
        min = inff;
        for(float v: X) {
            if(v > -inff) min = ::min(min, v);
            if(v < +inff) max = ::max(max, v);
        }
    }
    if(max==1) for(uint i: range(X.ref::size)) Y[i] = byte4(byte3(sRGB(X[i])), 0xFF);
    else for(uint i: range(X.ref::size)) Y[i] = byte4(byte3(sRGB(X[i] <= max ? (X[i]-min)/(max-min) : 1)), 0xFF);
}

void sRGB(const Image& Y, const Image3f& X, bgr3f max, bgr3f min) {
    if(max==bgr3f(-inff)) max = bgr3f(::max(::max(X)));
    if(min==bgr3f(+inff)) min = bgr3f(::min(::min(X)));
    assert_(Y.size == X.size);
    for(size_t y : range(Y.size.y)) {
        mref<byte4> dst = Y.slice(y*Y.stride, Y.size.x);
        ref<bgr3f> src = X.slice(y*X.stride, X.size.x);
        for(size_t x : range(Y.size.x)) dst[x] = byte4(sRGB((src[x]-min)/(max-min)), 0xFF);
    }
}

Image3f linear(const Image& source) {
    Image3f target (source.size);
    for(uint i: range(target.ref::size)) target[i] = bgr3f(sRGB_reverse[source[i].b], sRGB_reverse[source[i].g], sRGB_reverse[source[i].r]);
    return target;
}

ImageF luminance(const Image& source) {
    ImageF target (source.size);
    for(uint i: range(target.ref::size))
        target[i] = 0.0722f*sRGB_reverse[source[i].b]
                  + 0.7152f*sRGB_reverse[source[i].g]
                  + 0.2126f*sRGB_reverse[source[i].r];
    return target;
}

// -- Decode --

Image decodePNG(const ref<byte>);
__attribute((weak)) Image decodePNG(const ref<byte>) { error("PNG support not linked"); }
Image decodeJPEG(const ref<byte>);
__attribute((weak)) Image decodeJPEG(const ref<byte>) { log("JPEG support not linked"); return {}; }

Image decodeImage(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else error("Unknown image format");
}

// -- Rotate --

generic void rotateHalfTurn(const ImageT<T>& target) {
    for(size_t y: range(target.size.y)) for(size_t x: range(target.size.x/2)) swap(target(x,y), target(target.size.x-1-x, y)); // Reverse rows
    for(size_t y: range(target.size.y/2)) for(size_t x: range(target.size.x)) swap(target(x,y), target(x, target.size.y-1-y)); // Reverse columns
}
template void rotateHalfTurn(const Image8& target);

// -- Resample --

generic void downsample(const ImageT<T>& target, const ImageT<T>& source) {
    assert_(target.size == source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / T(4);
}
template void downsample(const ImageF& target, const ImageF&);
template void downsample(const Image3f& target, const Image3f&);

inline bgr3f linear(byte4 v) { return bgr3f(sRGB_reverse[v.b],sRGB_reverse[v.g],sRGB_reverse[v.r]); }

template<> void downsample(const Image& target, const Image& source) {
    assert_(target.size*2u == source.size);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        const byte4 v00 = source(x*2+0,y*2+0);
        const byte4 v01 = source(x*2+1,y*2+0);
        const byte4 v10 = source(x*2+0,y*2+1);
        const byte4 v11 = source(x*2+1,y*2+1);
        target(x,y) = byte4(sRGB((linear(v00) + linear(v01) + linear(v10) + linear(v11)) / 4.f),
                            (uint(v00.a) + uint(v01.a) + uint(v10.a) + uint(v11.a)) / 4u);
    }
}

generic void upsample(const ImageT<T>& target, const ImageT<T>& source) {
    assert_(target.size == source.size*2u);
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) {
        target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    }
}
template void upsample(const ImageF& target, const ImageF&);
template void upsample(const Image3f& target, const Image3f&);

// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
static void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
    int N = radius+1+radius;
    assert_(N < 1024, N);
    //chunk_parallel(height, [=](uint, size_t y) {
    for(size_t y: range(height)) {
        const float* line = source + y * sourceStride;
        float* targetColumn = target + y;
        if(width >= radius+1) {
            for(int x: range(-radius,0)) {
                float sum = 0;
                for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
                targetColumn[(x+radius)*targetStride] = sum;
            }
            for(int x: range(0,width-2*radius)) {
                float sum = 0;
                const float* span = line + x;
                for(int dx: range(N)) sum += kernel[dx] * span[dx];
                targetColumn[(x+radius)*targetStride] = sum;
            }
            assert_(width >= 2*radius);
            for(int x: range(width-2*radius,width-radius)){
                float sum = 0;
                for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(x+dx-(width-1))];
                targetColumn[(x+radius)*targetStride] = sum;
            }
        } else {
            for(int x: range(-radius, width-radius)) {
                float sum = 0;
                for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(abs(x+dx)-(width-1))];
                targetColumn[(x+radius)*targetStride] = sum;
            }
        }
    }
}

inline void operator*=(mref<float> values, float factor) { values.apply([factor](float v) { return factor*v; }, values); }

inline float exp(float x) { return __builtin_expf(x); }
inline float gaussian(float sigma, float x) { return exp(-sq(x/sigma)/2); }

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
    assert_(sigma > 0);
    if(!radius) radius = ceil(3*sigma);
    size_t N = radius+1+radius;
    //assert_(uint2(radius+1) <= source.size, sigma, radius, N, source.size);
    float kernel[N];
    for(int dx: range(N))
      kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
    float sum = ::sum(ref<float>(kernel,N), 0.);
    assert_(sum, ref<float>(kernel,N));
    mref<float>(kernel,N) *= 1/sum;
    buffer<float> transpose (target.size.y*target.size.x);
    convolve(transpose.begin(), source.begin(), kernel, radius, source.size.x, source.size.y, source.stride, source.size.y);
    assert_(source.size == target.size);
    convolve(target.begin(),  transpose.begin(), kernel, radius, target.size.y, target.size.x, target.size.y, target.stride);
}
