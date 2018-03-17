#include "thread.h"
#include "dng.h"
#include "window.h"
#include "image-render.h"
#include "png.h"
#if 1

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
