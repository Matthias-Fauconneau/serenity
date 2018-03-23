#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"
#include "mwc.h"
#include "matrix.h"
#include "jacobi.h"

template<> inline String str(const Matrix& A) {
    array<char> s;
    for(uint i: range(A.M)) {
        if(A.N==1) s.append("\t"+fmt(A(i,0), 4u));
        else {
            for(uint j: range(A.N)) {
                s.append("\t"+fmt(A(i,j), 4u));
            }
            if(i<A.M-1) s.append('\n');
        }
    }
    return move(s);
}

inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }
inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }

typedef ref<float> vector;

Vector operator*(const Matrix& A, const vector& x) {
    assert_(A.N == x.size);
    Vector y(A.M);
    for(int i: range(A.M)) { y[i]=0; for(int k: range(A.N)) y[i] += A(i,k)*x[k]; }
    return y;
}

static inline float length(const vector& x) { float s=0; for(float xi: x) s+=sq(xi); return sqrt(s); }

struct Test : Widget {
    Image preview;
    unique<Window> window = nullptr;

    Test() {
        const ImageF I = luminance(decodeImage(Map("test.jpg")));
        Array<uint, 256> histogram; histogram.clear(0);
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
        static constexpr uint N = 4;

        Array<vec2, 4> TX; {
            const ref<vec2> X = C;
            const vec2 μ = ::mean(X);
            TX.apply([=](const vec2 x){ return x-μ; }, X);
            const float μD = ::mean<float>(apply(X,[=](const vec2 x){ return ::length(x); }));
            TX.apply([=](const vec2 x){ return x*sqrt(2)/μD; }, TX);
        }
        log(TX);

        const ref<vec2> X´ = {{0,0},{210,0},{210,297},{0,297}}; // FIXME: normalize origin and average distance ~ √2
        Array<vec2, 4> TX´; {
            const ref<vec2> X = X´;
            const vec2 μ = ::mean<vec2>(X);
            TX´.apply([=](const vec2 x){ return x-μ; }, X);
            const float μD = ::mean<float>(apply(X,[=](const vec2 x){ return ::length(x); }));
            TX´.apply([=](const vec2 x){ return x*sqrt(2)/μD; }, TX´);
        }
        log(TX´);

        Matrix A(N*2, 9);
        for(uint i: range(N)) {
            const uint I = i*2;
            A(I+0, 0) = -TX[i].x;
            A(I+0, 1) = -TX[i].y;
            A(I+0, 2) = -1;
            A(I+0, 3) = 0; A(I+0, 4) = 0; A(I+0, 5) = 0;
            A(I+1, 0) = 0; A(I+1, 1) = 0; A(I+1, 2) = 0;
            A(I+1, 3) = -TX[i].x;
            A(I+1, 4) = -TX[i].y;
            A(I+1, 5) = -1;
            A(I+0, 6) = TX´[i].x*TX[i].x;
            A(I+0, 7) = TX´[i].x*TX[i].y;
            A(I+0, 8) = TX´[i].x;
            A(I+1, 6) = TX´[i].y*TX[i].x;
            A(I+1, 7) = TX´[i].y*TX[i].y;
            A(I+1, 8) = TX´[i].y;
        }
        log(A);
        const USV usv = SVD(A);
        log(usv.S);
        log(usv.V,'\n');
        const vector h = usv.V[usv.V.N-1]; //[usv.S.size-1];
        log(h);
        log(::length(h));
        log(A*h);
        log(::length(A*h));
        for(int j: range(A.N)) log(length(A*usv.V[j]));
        //for(int i: range(A.M)) assert_((A*h)[i]==0.f, (A*h)[i]);
        mat3 H;
        for(int i: range(usv.V.M)) H(i/3, i%3) = h[i];
        log(H);
        for(int k: range(N)) log(H*TX[k], TX´[k]);

        preview = sRGB(R);
        mat2 U = V.inverse();
        line(preview, μ+U*C[0], μ+U*C[1], {0,0,1});
        line(preview, μ+U*C[1], μ+U*C[2], {0,0,1});
        line(preview, μ+U*C[2], μ+U*C[3], {0,0,1});
        line(preview, μ+U*C[3], μ+U*C[0], {0,0,1});
        if(0) {
            window = ::window(this, int2(preview.size), mainThread, 0);
            window->show();
        }
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static test;
