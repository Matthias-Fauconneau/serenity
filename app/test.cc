#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "math.h"
#include "algorithm.h"

#if 0
static inline Image3f dxx(const Image3f& I) {
    Image3f dxx (I.size);
    for(int y: range(dxx.size.y))
        for(int x: range(dxx.size.x))
            dxx(x,y) = (1.f/4)*I(::max(0,x-1),y) + (-2.f/4)*I(x,y) + (1.f/4)*I(::min(int(I.size.x)-1,x+1),y);
    return dxx;
}

static inline Image3f dyy(const Image3f& I) {
    Image3f dyy (I.size);
    for(int y: range(dyy.size.y))
        for(int x: range(dyy.size.x))
            dyy(x,y) = (1.f/4)*I(x,::max(0,y-1)) + (-2.f/4)*I(x,y) + (1.f/4)*I(x,::min(int(I.size.y)-1,y+1));
    return dyy;
}

static inline Image3f dxy(const Image3f& I) {
    Image3f dxy (I.size);
    const float c = 1/(4*sqrt(2.));
    for(int y: range(dxy.size.y))
        for(int x: range(dxy.size.x))
            dxy(x,y) = +c*I(::max(0,x-1),::max(0,              y-1)) + -c*I(::min(int(I.size.x)-1,x+1),::max(0,              y-1))
                     + -c*I(::max(0,x-1),::min(int(I.size.y)-1,y+1)) + +c*I(::min(int(I.size.x)-1,x+1),::min(int(I.size.y)-1,y+1));
    return dxy;
}

static inline Image3f detH(const Image3f& I) {
    Image3f dxx = ::dxx(I);
    Image3f dyy = ::dyy(I);
    Image3f dxy = ::dxy(I);
    Image3f detH (I.size);
    for(uint i: range(detH.ref::size)) detH[i] = dxy[i]*dyy[i] - dxy[i]*dxy[i];
    return detH;
}
#endif

static inline Image3f circle(int size) {
    Image3f target = Image3f(uint2(size));
    const float R = (size-1.f)/2, R2 = sq(R);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r2 = sq(x-R)+sq(y-R);
        target(x,y) = bgr3f(r2<R2); // FIXME: antialiasing
    }
    return target;
}

static inline void negate(Image3f& target) { for(size_t i: range(target.ref::size)) target[i] = bgr3f(1)-target[i]; }
static inline Image3f negate(Image3f&& target) { negate(target); return ::move(target); }

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

static ImageF similarity(const Image3f& image, const Image3f& pattern) {
    int2 size = int2(image.size-pattern.size);
    ImageF similarity = ImageF( uint2(size) );
    for(int y: range(-size.y/2, size.y/2)) for(int x: range(-size.x/2, size.x/2)) {
        auto sse = SSE(image, pattern, int2(x,y));
        assert_(sse.count == uint(pattern.size.y*pattern.size.x), sse.count, pattern.size, x,y);
        similarity(size.x/2+x,size.y/2+y) = -sse.SSE;
    }
    return similarity;
}

struct Test : Widget {
    Time time {true};
    const Image3f image = downsample(downsample(downsample(linear(decodeImage(Map("test.jpg"))))));
    //const Image3f detH = ::detH(source);
    //const Image image = sRGB(detH, max(max(detH)));
    const Image3f circle = negate(::circle(image.size.y/4));
    const ImageF similarity = ::similarity(image, circle);
    const Image preview = sRGB(similarity);

    unique<Window> window = ::window(this, int2(preview.size), mainThread, 0);

    Test() {
        log(time);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
