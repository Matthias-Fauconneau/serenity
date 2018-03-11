#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "math.h"
#include "algorithm.h"

static inline Image3f circle(int size) {
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

#if 0
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
#else
static int2 argmaxSimilarity(const Image3f& image, const Image3f& pattern) {
    int2 size = abs(int2(image.size-pattern.size));
    int2 bestOffset = 0_0; float bestSimilarity = -inff; //-SSE..0
    for(int y: range(-size.y/2, size.y/2)) for(int x: range(-size.x/2, size.x/2)) {
        const int2 offset(x, y);
        const double similarity = -SSE(image, pattern, offset).SSE;
        assert_(similarity < inff);
        if(similarity > bestSimilarity) { bestSimilarity = similarity; bestOffset = offset; }
    }
    log(bestSimilarity, bestOffset, size);
    return bestOffset;
}
#endif

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

struct Test : Widget {
    Time time {true};
    const Image3f source = linear(decodeImage(Map("test.jpg")));
    //const Image3f detH = ::detH(image);
    //const Image image = sRGB(detH, max(max(detH)));
    const uint R = source.size.y/4;
    const uint D = 8;
    const Image3f image = downsample(downsample(downsample(source)));
    const Image3f circle = ::circle(R/D);
    const int2 center = ::argmaxSimilarity(negate(circle), image)*int(D);
    const Image preview = sRGB(multiply(::circle(R), source, center));

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
