#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "math.h"
#include "algorithm.h"

static inline Image3f disk(int size) {
    Image3f target = Image3f(uint2(size));
    const float R = (size-1.f)/2, R2 = sq(R);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r2 = sq(x-R)+sq(y-R);
        target(x,y) = bgr3f(r2<R2); // FIXME: antialiasing
    }
    return target;
}

static inline void negate(const Image3f& Y, const Image3f& X) { for(size_t i: range(Y.ref::size)) Y[i] = bgr3f(1)-X[i]; }
static inline Image3f negate(const Image3f& X) { Image3f Y(X.size); negate(Y,X); return Y; }

struct SSE_count { double SSE; uint count; };
inline SSE_count SSE(const Image3f& A, const Image3f& B, int2 centerOffset=0_0, uint64 minCount=0) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 const uint count = size.x*size.y;
 if(count < minCount) return {uint64(-1), 0};
 double SSE = 0;
 const bgr3f* a = A.data+aOffset.y*A.stride+aOffset.x;
 const bgr3f* b = B.data+bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const bgr3f* aLine = a+y*A.stride;
     const bgr3f* bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         SSE += dotSq(aLine[x] - bLine[x]);
     }
 }
 return {SSE, count};
}

static int2 argmaxSimilarity(const Image3f& A, const Image3f& B, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(A.size-B.size)); // Full search
    int2 bestOffset = 0_0; float bestSimilarity = -inff; //-SSE..0
    for(int y: range(-window.y/2, window.y/2)) for(int x: range(-window.x/2, window.x/2)) {
        const int2 offset = initialOffset + int2(x, y);
        const double similarity = -SSE(A, B, offset).SSE;
        assert_(similarity < inff);
        if(similarity > bestSimilarity) { bestSimilarity = similarity; bestOffset = offset; }
    }
    return bestOffset;
}

// Low resolution search and refine
static int2 align(const Image3f& A, const Image3f& B) {
#ifdef DEBUG
    return 0_0;
#endif
    const int D = 8;
    const int2 offset = ::argmaxSimilarity(downsample(downsample(downsample(A))), downsample(downsample(downsample(B))))*int(D);
    return offset; //::argmaxSimilarity(A, B, int2(D), offset);
}

generic void apply(const Image3f& A, const Image3f& B, int2 centerOffset, T f) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 const uint a = aOffset.y*A.stride+aOffset.x;
 const uint b = bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const uint yLine = y*size.x;
     const uint aLine = a+y*A.stride;
     const uint bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         f(yLine+x, aLine+x, bLine+x);
     }
 }
}

inline void multiply(const Image3f& Y, const Image3f& A, const Image3f& B, int2 centerOffset=0_0) {
    assert_(Y.size == ::min(A.size, B.size));
    apply(A, B, centerOffset, [&](const uint y, const uint a, const uint b){ Y[y]=A[a]*B[b]; });
}
inline Image3f multiply(const Image3f& A, const Image3f& B, int2 centerOffset=0_0) {
    Image3f Y(::min(A.size, B.size));
    multiply(Y,A,B,centerOffset);
    return Y;
}

inline void opEq(const ImageF& Y, const Image3f& X, bgr3f threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]==threshold); }
inline ImageF operator==(const Image3f& X, bgr3f threshold) { ImageF Y(X.size); ::opEq(Y,X,threshold); return Y; }

struct Test : Widget {
    Time time {true};
    const Image3f image = linear(decodeImage(Map("test.jpg")));
    const Image3f templateDisk = ::disk(image.size.y/4);
    const int2 center = ::align(negate(templateDisk), image);
    const ImageF disk = multiply(templateDisk, image, center) == bgr3f(1);
    Image preview = sRGB(disk);

    unique<Window> window = ::window(this, int2(preview.size), mainThread, 0);

    Test() {
        vec3 μ = 0_;
        for(uint iy: range(disk.size.y)) for(uint ix: range(disk.size.x)) {
            const float x = +((float(ix)/disk.size.x)*2-1);
            const float y = -((float(iy)/disk.size.y)*2-1);
            const float z = sqrt(1-(x*x+y*y));
            if(z <= 0) continue;
            const float ρ = 1/sq(z);
            const float w = 1/ρ;
            const float I = disk(ix, iy);
            μ += w*I*vec3(x,y,z);
        }
        μ = normalize(μ);
        int2 μ_xy = int2((vec2(μ.x,-μ.y)+vec2(1))/vec2(2)*vec2(disk.size));
        const int r=1; for(int dy: range(-r,r+1))for(int dx: range(-r,r+1)) preview(μ_xy.x+dx, μ_xy.y+dy) = byte4(0,0xFF,0,0xFF);
        log(time, μ);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
