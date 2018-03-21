#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"
#include "mwc.h"
#include "matrix.h"

inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }
inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }

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

        // Floodfill
        buffer<uint2> stack (I.ref::size, 0);
        stack.append(I.size/2u); // FIXME: Select largest region: floodfill from every unconnected seeds, keep largest region
        ImageT</*bool*/float> R (I.size); R.clear(0);
        while(stack) {
            const uint2& p0 = stack.pop();
            for(int2 dp: {int2(0,-1),int2(-1,0),int2(1,0),int2(0,1)}) { // 4-way connectivity
                uint2 p = uint2(int2(p0)+dp);
                if(anyGE(p,I.size)) continue;
                if(I(p) <= threshold) continue;
                if(R(p)) continue; // Already marked
                R(p) = 1;
                stack.append(p);
            }
        }

        // Mean
        buffer<vec2> X (R.ref::size, 0);
        vec2 Σ = 0_;
        for(const uint iy: range(I.size.y)) for(const uint ix: range(I.size.x)) {
            vec2 x(ix,iy);
            if(R(ix,iy)) {
                X.append(x);
                Σ += x;
            }
        }
        const vec2 μ = Σ / float(X.size);
        for(vec2& x: X) x -= μ;

        // PCA
        Random random;
        vec2 r = normalize(random.next<vec2>());
        for(auto_: range(4)) {
            vec2 Σ = 0_;
            for(vec2 x: X) Σ += dot(r,x)*x;
            r = normalize(Σ);
        }
        const vec2 e0 = r;
        const vec2 e1 = normal(e0);
        const mat2 V (e0, e1);

        // Initial corner estimation (maximize area of quadrant in eigenspace)
        vec2 C[4] = {0_,0_,0_,0_}; // eigenspace
        for(const vec2& x : X) {
            const vec2 Vx = V * x;
            static constexpr int quadrantToWinding[2][2] = {{0,1},{3,2}};
            vec2& c = C[quadrantToWinding[Vx.x>0][Vx.y>0]];
            if(abs(Vx.x*Vx.y) > abs(c.x*c.y)) c = Vx;
        }

        // Iterative corner optimization (maximize total area)
        for(uint i: range(4)) {
            const vec2 C3 = C[(i+3)%4];
            vec2& C0 = C[(i+0)%4];
            const vec2 C1 = C[(i+1)%4];
            float A0 = cross(C1-C0, C3-C0);
            for(const vec2& x : X) {
                const vec2 Vx = V * x;
                const float A = cross(C1-Vx, C3-Vx);
                if(A > A0) {
                    A0 = A;
                    C0 = Vx;
                }
            }
        }

        // DLT: x α Ay => Ba = 0
        const uint N = 4;
        float B[N][6];
        const vec3 Y[4] = {{0,0,0},{210,0,0},{210,297,0},{0,297,0}};
        for(uint k: range(N)) {
            B[k][0] = +C[k].y * Y[k].x;
            B[k][1] = -C[k].x * Y[k].x;
            B[k][2] = +C[k].y * Y[k].y;
            B[k][3] = -C[k].x * Y[k].y;
            B[k][4] = +C[k].y * Y[k].z;
            B[k][5] = -C[k].x * Y[k].z;
        }

        preview = sRGB(R);
        mat2 U = V.inverse();
        line(preview, μ+U*C[0], μ+U*C[1], {0,0,1});
        line(preview, μ+U*C[1], μ+U*C[2], {0,0,1});
        line(preview, μ+U*C[2], μ+U*C[3], {0,0,1});
        line(preview, μ+U*C[3], μ+U*C[0], {0,0,1});
        window = ::window(this, int2(preview.size), mainThread, 0);
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
