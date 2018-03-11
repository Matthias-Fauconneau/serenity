#include "image.h"
#include "data.h"
#include "vector.h"
#include "math.h"
#include "map.h"
#include "algorithm.h"

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
__attribute((constructor(1001))) void generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
        double linear = (double) index / (sizeof(sRGB_forward)-1);
        double sRGB = linear > 0.0031308 ? 1.055*__builtin_pow(linear,1/2.4)-0.055 : 12.92*linear;
        assert(abs(linear-(sRGB > 0.04045 ? __builtin_pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))< 0x1p-50);
        sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
__attribute((constructor(1002))) void generate_sRGB_reverse() {
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

void sRGB(const Image& BGR, const ImageF& Z) {
    float min = inff, max = -inff;
    for(float v: Z) {
        min = ::min(min, v);
        if(v < inff) max = ::max(max, v);
    }
    for(uint i: range(Z.ref::size)) BGR[i] = byte4(byte3(sRGB(Z[i] <= max ? (Z[i]-min)/(max-min) : 1)), 0xFF);
}

void sRGB(const Image& target, const Image3f& source, const float max) {
    assert_(target.size == source.size);
    for(size_t y : range(target.size.y)) {
        mref<byte4> dst = target.slice(y*target.stride, target.size.x);
        ref<bgr3f> src = source.slice(y*source.stride, source.size.x);
        for(size_t x : range(target.size.x)) dst[x] = byte4(sRGB(src[x].b/max), sRGB(src[x].g/max), sRGB(src[x].r/max), 0xFF);
    }
}

#if 0
void sRGB(const Image& target, const Image4f& source) {
    assert_(target.size == source.size);
    for(size_t y : range(target.size.y)) {
        mref<byte4> dst = target.slice(y*target.stride, target.size.x);
        ref<bgra4f> src = source.slice(y*source.stride, source.size.x);
        for(size_t x : range(target.size.x)) dst[x] = byte4(sRGB(src[x].b), sRGB(src[x].g), sRGB(src[x].r), 0xFF);
    }
}

void sRGB(const Image& BGR, const ImageF& B, const ImageF& G, const ImageF& R) {
    assert_(BGR.size == B.size && B.size == G.size && G.size == R.size, BGR.size, B.size, G.size, R.size);
    assert_(B.stride == G.stride && G.stride == R.stride);
    for(size_t y : range(BGR.size.y)) {
        mref<byte4> bgr = BGR.slice(y*BGR.stride, BGR.size.x);
        ref<float> b = B.slice(y*B.stride, B.size.x);
        ref<float> g = G.slice(y*G.stride, G.size.x);
        ref<float> r = R.slice(y*R.stride, R.size.x);
        for(size_t x : range(BGR.size.x)) bgr[x] = byte4(sRGB(b[x]), sRGB(g[x]), sRGB(r[x]), 0xFF);
    }
}

void sRGB(const Image& BGR, const Image16& N, const ImageF& B, const ImageF& G, const ImageF& R) {
    assert_(BGR.size == N.size && B.size == N.size && G.size == N.size && R.size == N.size);
    assert_(B.stride == N.stride && G.stride == N.stride && R.stride == N.stride);
    for(size_t y : range(BGR.size.y)) {
        mref<byte4> bgr = BGR.slice(y*BGR.stride, BGR.size.x);
        ref<uint16> n = N.slice(y*N.stride, N.size.x);
        ref<float> b = B.slice(y*B.stride, B.size.x);
        ref<float> g = G.slice(y*G.stride, G.size.x);
        ref<float> r = R.slice(y*R.stride, R.size.x);
        for(size_t x : range(BGR.size.x)) bgr[x] = byte4(sRGB(b[x]/n[x]), sRGB(g[x]/n[x]), sRGB(r[x]/n[x]), 0xFF);
    }
}

void sRGB(const Image& BGR, const Image8& M) {
    const uint max = ::max(M);
    for(uint i: range(M.ref::size)) BGR[i] = byte4(byte3(0xFF*uint(M[i])/max), 0xFF);
}
void sRGB(const Image& BGR, const Image16& N) {
    const uint max = ::max(N);
    for(uint i: range(N.ref::size)) BGR[i] = byte4(byte3(0xFF*uint(N[i])/max), 0xFF);
}
#endif

Image3f linear(const Image& source) {
    Image3f target (source.size);
    for(uint i: range(target.ref::size)) target[i] = bgr3f(sRGB_reverse[source[i].b], sRGB_reverse[source[i].g], sRGB_reverse[source[i].r]);
    return target;
}

// -- Decode --

#if 0
string imageFileFormat(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
    else if(startsWith(file,"\x89PNG\r\n\x1A\n"_)) return "PNG"_;
    else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
    else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
    else if(startsWith(file,"BM"_)) return "BMP"_;
    else return ""_;
}


int2 imageSize(const ref<byte> file) {
    BinaryData s(file, true);
    // PNG
    if(s.match(ref<uint8>{0b10001001,'P','N','G','\r','\n',0x1A,'\n'})) {
        for(;;) {
            s.advance(4); // Length
            if(s.read<byte>(4) == "IHDR"_) {
                uint width = s.read(), height = s.read();
                return int2(width, height);
            }
        }
        error("PNG");
    }
    // JPEG
    enum Marker : uint8 {
        StartOfFrame = 0xC0, DefineHuffmanTable = 0xC4, StartOfImage = 0xD8, EndOfImage = 0xD9,
        StartOfSlice = 0xDA, DefineQuantizationTable = 0xDB, DefineRestartInterval = 0xDD, ApplicationSpecific = 0xE0 };
    if(s.match(ref<uint8>{0xFF, StartOfImage})) {
        for(;;){
            s.skip((uint8)0xFF);
            uint8 marker = s.read();
            if(marker == EndOfImage) break;
            if(marker==StartOfSlice) {
                while(s.available(2) && ((uint8)s.peek() != 0xFF || uint8(s.peek(2)[1])<0xC0)) s.advance(1);
            } else {
                uint16 length = s.read(); // Length
                if(marker>=StartOfFrame && marker<=StartOfFrame+2) {
                    uint8 precision = s.read(); assert_(precision==8);
                    uint16 height = s.read();
                    uint16 width = s.read();
                    return int2(width, height);
                    //uint8 components = s.read();
                    //for(components) { ident:8, h_samp:4, v_samp:4, quant:8 }
                } else s.advance(length-2);
            }
        }
        error("JPG");
    }
    error("Unknown image format");
}
#endif

Image decodePNG(const ref<byte>);
__attribute((weak)) Image decodePNG(const ref<byte>) { error("PNG support not linked"); }
Image decodeJPEG(const ref<byte>);
__attribute((weak)) Image decodeJPEG(const ref<byte>) { log("JPEG support not linked"); return {}; }

Image decodeImage(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
    else error("Unknown image format");
}

#if 0
// -- Rotate --

void flip(const Image& target, const Image& source) {
    assert_(target.size == source.size);
    const uint h = source.size.y;
    for(uint y: range(h)) {
        byte4* A = source.begin()+(    y)*source.stride;
        byte4* B = target.begin()+(h-1-y)*target.stride;
        for(uint x: range(source.size.x)) B[x] = A[x];
    }
}
generic void flip(const ImageT<T>& image) {
    const uint h = image.size.y;
    for(uint y: range(h/2)) {
        T* A = image.begin()+(    y)*image.stride;
        T* B = image.begin()+(h-1-y)*image.stride;
        for(uint x: range(image.size.x)) swap(A[x],B[x]);
    }
}
template void flip(const ImageT<uint8>&);
template void flip(const ImageT<uint16>&);
template void flip(const ImageT<float>&);
template void flip(const ImageT<byte4>&);
template void flip(const ImageT<bgra4f>&);

Image flip(Image&& image) {
    flip(image);
    return move(image);
}

void negate(const Image& target, const Image& source) {
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        byte4 BGRA = source(x, y);
        vec3 linear = vec3(sRGB_reverse[BGRA[0]], sRGB_reverse[BGRA[1]], sRGB_reverse[BGRA[2]]);
        int3 negate = int3(round((float(0xFFF)*(vec3(1)-linear))));
        target(x,y) = byte4(sRGB_forward[negate[0]], sRGB_forward[negate[1]], sRGB_forward[negate[2]], BGRA.a);
    }
}
#endif

// -- Resample --

Image3f downsample(Image3f&& target, const Image3f& source) {
    assert_(target.size == source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / bgr3f(4);
    return move(target);
}

void upsample(const Image3f& target, const Image3f& source) {
    assert_(target.size == source.size*2u);
    for(uint y: range(source.size.y)) for(uint x: range(source.size.x)) {
        target(x*2+0,y*2+0) = target(x*2+1,y*2+0) = target(x*2+0,y*2+1) = target(x*2+1,y*2+1) = source(x,y);
    }
}

#if 0
static void bilinear(const Image& target, const Image& source) {
    //assert_(!source.alpha, source.size, target.size);
    const uint stride = source.stride;
    for(uint y: range(target.size.y)) {
        for(uint x: range(target.size.x)) {
            const uint fx = x*256*(source.size.x-1)/target.size.x, fy = y*256*(source.size.y-1)/target.size.y; //TODO: incremental
            uint ix = fx/256, iy = fy/256;
            uint u = fx%256, v = fy%256;
            const ref<byte4> span = source.slice(iy*stride+ix);
            const uint a  = ((uint(span[      0][3]) * (256-u) + uint(span[           1][3])  * u) * (256-v)
                    + (uint(span[stride][3]) * (256-u) + uint(span[stride+1][3]) * u) * (       v) ) / (256*256);
            byte4 d;
            if(!a) d = byte4(0,0,0,0);
            else {
                for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
                    d[i] = ((uint(span[      0][3]) * uint(span[      0][i]) * (256-u) + uint(span[           1][3]) * uint(span[           1][i]) * u) * (256-v)
                            + (uint(span[stride][3]) * uint(span[stride][i]) * (256-u) + uint(span[stride+1][3]) * uint(span[stride+1][i]) * u) * (       v) )
                            / (a*256*256);
                }
                d[3] = a;
            }
            target(x, y) = d;
        }
    }
}

void resize(const Image& target, const Image& source) {
    assert_(source && target && source.size != target.size);
    if(target.size > source.size/2u) bilinear(target, source); // Bilinear resample
    else error("resize", target.size, source.size);
}
#endif
