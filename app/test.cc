#include "thread.h"
#if 1
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"

static inline ImageF dxx(const ImageF& I) {
    ImageF dxx (I.size);
    for(int y: range(dxx.size.y))
        for(int x: range(dxx.size.x))
            dxx(x,y) = (1.f/4)*I(::max(0,x-1),y) + (-2.f/4)*I(x,y) + (1.f/4)*I(::min(int(I.size.x)-1,x+1),y);
    return dxx;
}

static inline ImageF dyy(const ImageF& I) {
    ImageF dyy (I.size);
    for(int y: range(dyy.size.y))
        for(int x: range(dyy.size.x))
            dyy(x,y) = (1.f/4)*I(x,::max(0,y-1)) + (-2.f/4)*I(x,y) + (1.f/4)*I(x,::min(int(I.size.y)-1,y+1));
    return dyy;
}

static inline ImageF dxy(const ImageF& I) {
    ImageF dxy (I.size);
    const float c = 1/(4*sqrt(2.));
    for(int y: range(dxy.size.y))
        for(int x: range(dxy.size.x))
            dxy(x,y) = +c*I(::max(0,x-1),::max(0,              y-1)) + -c*I(::min(int(I.size.x)-1,x+1),::max(0,              y-1))
                     + -c*I(::max(0,x-1),::min(int(I.size.y)-1,y+1)) + +c*I(::min(int(I.size.x)-1,x+1),::min(int(I.size.y)-1,y+1));
    return dxy;
}

static inline ImageF detH(const ImageF& I) {
    ImageF dxx = ::dxx(I);
    ImageF dyy = ::dyy(I);
    ImageF dxy = ::dxy(I);
    ImageF detH (I.size);
    for(uint i: range(detH.ref::size)) detH[i] = dxx[i]*dyy[i] - dxy[i]*dxy[i];
    return detH;
}

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const ImageF X = luminance(decodeImage(Map("test.jpg")));

        /*for(int r: range(1))*/ //{
            const ImageF x = gaussianBlur(X, 1);
            //ImageF Y (X.size);
            //for(uint i: range(Y.ref::size)) Y[i] = abs(X[i]-x[i]);
            //return Y;
        //}

        const ImageF detH = ::detH(x);

        ImageF max (detH.size);
        max.clear(0); // FIXME: borders only
        for(uint y0: range(1, detH.size.y-1)) {
            for(uint x0: range(1, detH.size.x-1)) {
                const float c = detH(x0, y0);
                for(uint y: range(y0-1, y0+1 +1)) for(uint x: range(x0-1, x0+1 +1)) {
                        if((y!=y0||x!=x0) && abs(detH(x,y)) >= abs(c)) goto break_;
                } /*else*/ max(x0, y0) = abs(detH(x0, y0));
                /**/ break_:;
            }
        }
        log(sum((max/x) > 0.0001f));
        preview = sRGB((max/x) > 0.0001f);
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;

#elif 1
#include "dng.h"

// Splits CFA R,GG,B quads into BGR components, and normalizes min/max levels, yields RGGB intensity image
static Image3f BGGRtoBGR(const DNG& source) {
    Image3f target(source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        const int B  = ::max(0, source(x*2+0,y*2+0)-source.blackLevel);
        const int G1 = ::max(0, source(x*2+1,y*2+0)-source.blackLevel);
        const int G2 = ::max(0, source(x*2+0,y*2+1)-source.blackLevel);
        const int R  = ::max(0, source(x*2+1,y*2+1)-source.blackLevel);
        const float rcp = 4095-source.blackLevel;
        const float b = rcp*B;
        const float g = (rcp/2)*(G1+G2);
        const float r = rcp*R;
        target(x,y) = bgr3f(b,g,r);
    }
    return target;
}

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const Image3f background = BGGRtoBGR(parseDNG(Map("IMG_0752.dng")));
        const Image3f shadow = BGGRtoBGR(parseDNG(Map("IMG_0751.dng")));
        const Image3f ratio (shadow.size);
        for(const uint i: range(ratio.ref::size)) ratio[i] = shadow[i] / background[i];
        preview = sRGB(ratio);
        writeFile("ratio.png", encodePNG(preview));
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;

#else

#include "vector.h"
#include "math.h"
struct Test {
    Test() {
        const vec3 A (0.22, 0.25, 0.94);
        const vec3 B (0.20, 0.34, 0.92);
        log(acos(dot(A,B)/sqrt(dotSq(A)*dotSq(B)))*180/π);
    }
} static test;
#endif
