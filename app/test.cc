#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"


struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const ImageF X = luminance(decodeImage(Map("test.jpg")));
        Array(uint, histogram, 256); histogram.clear(0);
        const float maxX = ::max(X);
        for(const float x: X) histogram[int((histogram.size-1)*x/maxX)]++;

        const uint totalCount = X.ref::size;
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

        preview = Image(X.size);
        for(const uint y: range(1, X.size.y-1)) for(const uint x: range(1, X.size.x-1)) {
            bool anyBackground = false;
            for(const int dy: range(-1, 1 +1)) for(const int dx: range(-1, 1 +1)) {
                if(X(x+dx, y+dy) < threshold) anyBackground = true;
            }
            if(X(x,y) > threshold && anyBackground) preview(x,y) = byte4(0xFF);
        }

        //preview = sRGB(X > threshold);
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
