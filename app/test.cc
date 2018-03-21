#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"
#include "mwc.h"

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const ImageF I = luminance(decodeImage(Map("test.jpg")));
        Array(uint, histogram, 256); histogram.clear(0);
        const float maxX = ::max(I);
        for(const float x: I) histogram[int((histogram.size-1)*x/maxX)]++;

        const uint totalCount = I.ref::size;
        uint64 totalSum = 0;
        for(uint t: range(histogram.size)) totalSum += t*histogram[t];
        uint backgroundCount = 0;
        uint backgroundSum = 0;
        float maximumVariance = 0;
        uint thresholdIndex = 0;
        for(uint t: range(histogram.size)) {
            backgroundCount += histogram[t];
            if(backgroundCount == 0) continue;
            backgroundSum += t*histogram[t];
            uint foregroundCount = totalCount - backgroundCount;
            uint64 foregroundSum = totalSum - backgroundSum;
            if(foregroundCount == 0) break;
            const float foregroundMean = float(foregroundSum)/float(foregroundCount);
            const float backgroundMean = float(backgroundSum)/float(backgroundCount);
            const float variance = float(foregroundCount)*float(backgroundCount)*sq(foregroundMean - backgroundMean);
            if(variance >= maximumVariance) {
                maximumVariance=variance;
                thresholdIndex = t;
            }
        }
        const float threshold = float(thresholdIndex)/float(histogram.size-1) * maxX;

        buffer<vec2> X (I.ref::size, 0);
        vec2 Σ = 0_;
        for(const uint iy: range(I.size.y)) for(const uint ix: range(I.size.x)) {
            vec2 x(ix,iy);
            if(I(ix,iy) > threshold) {
                X.append(x);
                Σ += x;
            }
        }
        const vec2 μ = Σ / float(X.size);
        for(vec2& x: X) x -= μ;
        Random random;
        vec2 r = normalize(random.next<vec2>());
        for(auto_: range(4)) {
            vec2 Σ = 0_;
            for(vec2 x: X) Σ += dot(r,x)*x;
            r = normalize(Σ);
        }
        log(r);

        preview = sRGB(I > threshold);
        line(preview, vec2(preview.size)/2.f, vec2(preview.size)/2.f+r*vec2(preview.size));

        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
