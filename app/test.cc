#include "thread.h"
#include "window.h"
#include "image-render.h"
#include "jpeg.h"
#include "algorithm.h"
#include "mwc.h"
#include "matrix.h"
#include "jacobi.h"
#include "video.h"
#include "plot.h"
#include "png.h"
#include "sort.h"

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

// Right handed
inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }
inline float cross(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }
inline int cross(uint2 a, uint2 b) { return int(a.x*b.y) - int(a.y*b.x); }

typedef ref<float> vector;

static uint draw(Random& random, const ref<uint> DPD, const uint64 sum) {
    uint64 u = random.next<uint64>()%sum;
    uint64 p = 0;
    for(const uint i: range(DPD.size)) {
        p += DPD[i];
        if(p > u) return i;
    }
    error("");
}

struct Test : Widget {
    Decoder video {"test.mp4"};
    unique<Window> window = nullptr;

    ImageT</*bool*/float> R;

    vec2 μ, e0, e1;

    const float y = 210./297;
    const ref<vec2> modelC = {{-1,-y},{1,-y},{1,y},{-1,y}}; // FIXME: normalize origin and average distance ~ √2

    mat4 M;

    uint frameIndex = 0;

    Test() {
        for(auto_: range(1)) step();
        //while(step()) {}
        window = ::window(this, int2(R.size/*/2u*/), mainThread, 0);
        window->show();
        //window->actions[Space] = [this]{ step(); window->render(); };
    }

    bool step() {
        //log(frameIndex);
        frameIndex++;
        //const ImageF I = luminance(rotateHalfTurn(decodeImage(Map("test.jpg"))));
        Time time {true};
        if(!video.read()) return false;
        //const ImageF I = luminance(video.YUV(0));
        const Image8 Y = video.YUV(0);
        time.reset(); //log("Decode", fmt(time.reset().milliseconds())+"ms"_);
        Array<uint, 256> histogram; histogram.clear(0);
        //const float maxY = ::max(Y);
        //for(const float x: I) histogram[int((histogram.size-1)*x/maxX)]++;
        for(const uint8 y: Y) histogram[y]++;
        const uint totalCount = Y.ref::size;

#if 1 // K-means++ (FIXME: parameter K (=3))
        uint threshold;
        {
            const uint K = 3;
            buffer<uint> clusters (K);
            Random random;
            clusters[0] = draw(random, histogram, totalCount);
            for(uint k: range(1, clusters.size)) {
                Array(uint, DPD, histogram.size); // FIXME: storing cDPD directly would allow to binary search in ::draw
                uint64 sum = 0;
                for(const uint i: range(histogram.size)) {
                    uint D = -1;
                    for(uint cluster: clusters.slice(0,k)) D = ::min(D, (uint)sq(int(i)-int(cluster)));
                    D *= histogram[i]; // Samples according to distance x density
                    DPD[i] = D;
                    sum += D;
                }
                const uint i = draw(random, DPD, sum);
                assert_(histogram[i], i, DPD[i]);
                clusters[k] = i;
            }

            /*for(auto_: range(4))*/ for(;;) {
                Array(uint64, Σ, clusters.size); Σ.clear();
                Array(uint, N, clusters.size); N.clear();
                for(const uint i: range(histogram.size)) {
                    uint bestD = -1;
                    uint k = -1;
                    for(const uint ik: range(clusters.size)) {
                        const uint D = sq(int(i)-int(clusters[ik]));
                        if(D < bestD) {
                            bestD = D;
                            k = ik;
                        }
                    }
                    assert_(k<clusters.size);
                    for(const uint ik: range(clusters.size)) if(clusters[ik]==i) assert_(k==ik);
                    N[k] += histogram[i];
                    Σ[k] += histogram[i]*i;
                }
                bool changed = false;
                for(const uint k: range(clusters.size)) {
                    assert_(N[k], clusters, histogram[clusters[0]]);
                    uint c = Σ[k]/N[k];
                    if(clusters[k] != c) changed = true;
                    clusters[k] = c;
                }
                //log(Σ, N, clusters);
                sort(clusters);
                if(!changed) break;
            }
            threshold = ((clusters[K-3]+clusters[K-2])/2+clusters[K-1])/2;
            //log(clusters, threshold);
        }
#else // Otsu
        uint totalSum = 0;
        for(uint t: range(histogram.size)) totalSum += t*histogram[t];
        uint backgroundCount = 0;
        uint backgroundSum = 0;
        float maximumVariance = 0;
        uint thresholdIndex = 0;
        map<float, float> σ;
        for(const uint t: range(histogram.size)) {
            backgroundCount += histogram[t];
            if(backgroundCount == 0) continue;
            backgroundSum += t*histogram[t];
            const uint foregroundCount = totalCount - backgroundCount;
            const uint foregroundSum = totalSum - backgroundSum;
            if(foregroundCount == 0) break;
            const float backgroundMean = float(backgroundSum)/float(backgroundCount);
            const float foregroundMean = float(foregroundSum)/float(foregroundCount);
            const float variance = float(backgroundCount)*float(foregroundCount)*sq(foregroundMean - backgroundMean);
            //log(t, histogram[t], variance);
            //log(backgroundCount, backgroundSum, backgroundMean);
            //log(foregroundCount, foregroundSum, foregroundMean);
            log(t, backgroundCount, foregroundCount, backgroundMean, foregroundMean, sqrt(variance));
            σ.insert(t, sqrt(variance));
            if(variance >= maximumVariance) {
                maximumVariance=variance;
                thresholdIndex = t;
            }
        }
        {
            for(float& s: σ.values) s /= sqrt(maximumVariance);
            map<float,float> H;
            for(const uint i: range(histogram.size)) H.insert(i, float(histogram[i])/float(::max(histogram)));
            Plot plot;
            plot.dataSets.insert("H"__, move(H));
            plot.dataSets.insert("σ"__, move(σ));
            ImageRenderTarget target(uint2(3840,2160));
            target.clear(byte4(0xFF));
            plot.render(target);
            writeFile("plot.png", encodePNG(target), currentWorkingDirectory(), true);
            error("plot");
        }
        error(thresholdIndex);
        //error(ref<float>(σ, histogram.size));
        //const float threshold = float(thresholdIndex)/float(histogram.size-1) * maxX;
        const uint threshold = thresholdIndex;
#endif

#if 1 // Hull of main region
        // Floodfill outside
        buffer<uint2> stack (Y.ref::size, 0);
        // Assumes one of the edge connects to the main background
        for(int x: range(Y.size.x)) {
            stack.append(uint2(x,0));
            stack.append(uint2(x,Y.size.y-1));
        }
        for(int y: range(Y.size.y)) {
            stack.append(uint2(0,y));
            stack.append(uint2(Y.size.x-1,y));
        }

        R = ImageT</*bool*/float>(Y.size); R.clear(1);

        time.reset();
        while(stack) {
            const uint2& p0 = stack.pop();
            for(int2 dp: {int2(0,-1),int2(-1,0),int2(1,0),int2(0,1)}) { // 4-way connectivity
                uint2 p = uint2(int2(p0)+dp);
                if(anyGE(p, R.size)) continue;
                if(Y(p) > threshold) continue;
                if(R(p) == 0) continue; // Already marked
                R(p) = 0;
                stack.append(p);
            }
        }
        time.reset(); //log("Floodfill", fmt(time.reset().milliseconds())+"ms"_);

        uint2 start = R.size/2u;
        while(R(start+uint2(1,0))) start.x++;
        buffer<uint2> H (R.ref::size, 0); // Hull
        uint2 p = start;
        for(;;) { // Walk CCW
            const int2 CCW[8] = {int2(-1,-1),int2(-1, 0),int2(-1,+1),int2(0,+1),int2(1,+1),int2(1, 0),int2(1,-1),int2(0,-1)};
            const uint previousSize = H.size;
            for(int i: range(8)) { // Searches for a CCW background->foreground transition
                uint2 bg (int2(p)+CCW[i]);
                uint2 fg (int2(p)+CCW[(i+1)%8]);
                if(R(bg)==0 && R(fg)==1) { // Assumes only one Bg->Fg transition (no holes)
                    //assert_(H.size < H.capacity, H.size, H.capacity, start, p, bg, fg); // Infinite loop
                    //if(!(H.size < H.capacity)) { log(frameIndex, H.size, H.capacity, start, p, bg, fg, H.slice(H.size-256, 256)); return false; }
                    H.append(fg);
                }
            }
            uint nofTransitions = H.size - previousSize;
            if(nofTransitions == 2) {
                assert_(H.size >= 4);
                if(H[H.size-1] == H[H.size-4]) H.pop();
                else if(H[H.size-2] == H[H.size-4]) { H[H.size-2]=H[H.size-1]; H.pop(); }
                nofTransitions = H.size - previousSize;
            }
            if(nofTransitions == 1) {
                p = H.last();
            } else {
                log(H);
                log(nofTransitions);
                {
                    uint count = 0;
                    for(int i: range(8)) { // Counts foreground neighbours
                        uint2 n (int2(p)+CCW[i]);
                        if(R(n)) count++;
                    }
                    log(p, count);
                }
                for(uint2 p: H.slice(previousSize)) {
                    uint count = 0;
                    for(int i: range(8)) { // Counts foreground neighbours
                        uint2 n (int2(p)+CCW[i]);
                        if(R(n)) count++;
                    }
                    log(p, count);
                }
                array<char> S;
                for(uint y: range(p.y-2, p.y+2 +1)) {
                    for(uint x: range(p.x-2, p.x+2 +1)) {
                        S.append(R(x,y)?'x':'.');
                    }
                    S.append('\n');
                }
                log(S);
                error(nofTransitions);
            }
            if(p == start) break;
        }

        //if(H.size > 3 && cross(H[H.size-1]-H[H.size-2], H[H.size-2]-H[H.size-3]) <= 0) H.pop();

        R = ImageT</*bool*/float>(Y.size); R.clear(0); //
        for(uint2 h: H) R(h) = 1;
#else // PCA
        // Floodfill
        buffer<uint2> stack (Y.ref::size, 0);
        stack.append(Y.size/2u); // FIXME: Select largest region: floodfill from every unconnected seeds, keep largest region
        R = ImageT</*bool*/float>(Y.size); R.clear(0);

        //for(uint i: range(R.ref::size)) R[i] = Y[i] > threshold; //Y[i]/255.f;
        //log("K-Means++", fmt(time.reset().milliseconds())+"ms"_);

        time.reset();
        while(stack) {
            const uint2& p0 = stack.pop();
            for(int2 dp: {int2(0,-1),int2(-1,0),int2(1,0),int2(0,1)}) { // 4-way connectivity
                uint2 p = uint2(int2(p0)+dp);
                if(anyGE(p, R.size)) continue;
                if(Y(p) <= threshold) continue;
                if(R(p)) continue; // Already marked
                R(p) = 1;
                stack.append(p);
            }
        }
        time.reset(); //log("Floodfill", fmt(time.reset().milliseconds())+"ms"_);

        {
            ImageT</*bool*/float> E(Y.size); E.clear(0);
            for(const uint y: range(1, R.size.y-1)) for(const uint x: range(1, R.size.x-1)) {
                if(!R(x,y)) continue;
                for(const int dy: range(-1, 1 +1)) for(const int dx: range(-1, 1 +1)) {
                    if(!R(x+dx, y+dy)) goto break_;
                } /*else*/ continue;
                break_:
                E(x,y) = 1;
            }
            R = move(E);
        }
        time.reset(); //log("Edge", fmt(time.reset().milliseconds())+"ms"_);

        // Mean
        buffer<vec2> X (R.ref::size, 0);
        vec2 Σ = 0_;
        for(const uint iy: range(R.size.y)) for(const uint ix: range(R.size.x)) {
            vec2 x(ix, R.size.y-1-iy); // Flips Y axis from Y top down to Y bottom up
            if(R(ix,iy)) {
                X.append(x);
                Σ += x;
            }
        }
        /*const vec2*/ μ = Σ / float(X.size);
        for(vec2& x: X) x -= μ;

        // PCA
        Random random;
        vec2 r = normalize(vec2(0.37, 0.93)); //normalize(random.next<vec2>()); // FIXME: unstable
        //log(r);
        for(auto_: range(4)) {
            vec2 Σ = 0_;
            for(vec2 x: X) Σ += dot(r,x)*x;
            r = normalize(Σ);
        }
        /*const vec2*/ e0 = r;
        /*const vec2*/ e1 = normal(e0);
        const mat2 V (e0, e1);
        //log(V); // 1 0.02, -0.02 1

        // Initial corner estimation (maximize area of quadrant in eigenspace)
        vec2 C[4] = {0_,0_,0_,0_}; // eigenspace
        for(const vec2& x : X) {
            const vec2 Vx = V * x;
            static constexpr int quadrantToWinding[2][2] = {{0,3},{1,2}};
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
#endif
        log("Corner", fmt(time.reset().milliseconds())+"ms"_);
#if 0
        static constexpr uint N = 4;

        mat2 U = V.inverse();
        mat3 K;
        const float focalLength = 4.2, pixelPitch = 0.0014;
        K(0,0) = 2/(R.size.x*pixelPitch/focalLength);
        K(1,1) = 2/(R.size.y*pixelPitch/focalLength);
        K(2,2) = 1;
        const mat3 K¯¹ = K.¯¹();
        const buffer<vec2> X´ = apply(ref<vec2>(C), [&](vec2 x){ return K¯¹*(2.f*(μ+U*x)/vec2(R.size)-vec2(1)); });

        const ref<vec2> TX = modelC;
        const ref<vec2> TX´ = X´;

        // DLT: Ah = 0
        Matrix A(N*2, 9);
        for(uint i: range(N)) {
            const uint I = i*2;
            A(I+0, 0) = -TX[i].x;
            A(I+0, 1) = -TX[i].y;
            A(I+0, 2) = -1; // -1
            A(I+0, 3) = 0; A(I+0, 4) = 0; A(I+0, 5) = 0;
            A(I+1, 0) = 0; A(I+1, 1) = 0; A(I+1, 2) = 0;
            A(I+1, 3) = -TX[i].x;
            A(I+1, 4) = -TX[i].y;
            A(I+1, 5) = -1; // -1
            A(I+0, 6) = TX´[i].x*TX[i].x;
            A(I+0, 7) = TX´[i].x*TX[i].y;
            A(I+0, 8) = TX´[i].x; // -1
            A(I+1, 6) = TX´[i].y*TX[i].x;
            A(I+1, 7) = TX´[i].y*TX[i].y;
            A(I+1, 8) = TX´[i].y; // -1
        }
        const USV usv = SVD(A);
        const vector h = usv.V[usv.V.N-1];
        mat3 H;
        for(int i: range(usv.V.M)) H(i/3, i%3) = h[i];// / h[8];
        H = mat3(vec3(1/sqrt(::length(H[0])*::length(H[1])))) * H; // Normalizes by geometric mean of the 2 rotation vectors

        mat4 Rt;
        Rt[0] = vec4(H[0], 0);
        Rt[1] = vec4(H[1], 0);
        Rt[2] = vec4(cross(H[0],H[1]), 0);
        Rt[3] = vec4(-H[2], 1); // Z-
        Rt = mat4(vec4(-1,-1,1,1)) * Rt; // Flips X & Y as well
        assert_(abs(1-((mat3)Rt).det())<0.05,((mat3)Rt).det(), abs(1-((mat3)Rt).det()));

        const float near = K(1,1);
        const float far = 1000/focalLength*near; //mm
        const mat4 projection = perspective(near, far).scale(vec3(float(R.size.y)/float(R.size.x), 1, 1));
        const mat4 NDC = mat4()
                .scale(vec3(vec2(R.size)/2.f, 1))
                .translate(vec3(1)); // -1, 1 -> 0, 2
        const mat4 flipY = mat4().translate(vec3(0, R.size.y-1, 0)).scale(vec3(1, -1, 1)); // Flips Y axis from Y bottom up to Y top down for ::line
        M = flipY*NDC*projection*Rt;
#endif
        return true;
    }
    void render(const Image& target) {
        Time time {true};
        sRGB(target, R, 1);

        const mat3 flipY2 = mat3().translate(vec2(0, R.size.y-1)).scale(vec2(1, -1)); // Flips Y axis from Y bottom up to Y top down for ::line
        line(target, flipY2*μ, flipY2*(μ+256.f*e0), bgr3f(0,0,1));
        line(target, flipY2*μ, flipY2*(μ+256.f*e1), bgr3f(0,1,0));

        line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[1], 0)).xy(), bgr3f(0,0,1));
        line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[2], 0)).xy(), bgr3f(0,1,0));
        line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[3], 0)).xy(), bgr3f(1,0,0));
        line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[0], 0)).xy(), bgr3f(0,1,1));

        const float z = 0.1;
        line(target, (M*vec3(modelC[0], 0)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[1], 0)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[2], 0)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(1));
        line(target, (M*vec3(modelC[3], 0)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1));

        line(target, (M*vec3(modelC[0], z)).xy(), (M*vec3(modelC[1], z)).xy(), bgr3f(0,0,1));
        line(target, (M*vec3(modelC[1], z)).xy(), (M*vec3(modelC[2], z)).xy(), bgr3f(0,1,0));
        line(target, (M*vec3(modelC[2], z)).xy(), (M*vec3(modelC[3], z)).xy(), bgr3f(1,0,0));
        line(target, (M*vec3(modelC[3], z)).xy(), (M*vec3(modelC[0], z)).xy(), bgr3f(0,1,1));
        //log("Render", fmt(time.reset().milliseconds())+"ms"_);
    }
    void render(RenderTarget2D& renderTarget_, vec2, vec2) override {
        const Image& renderTarget = (ImageRenderTarget&)renderTarget_;
        render(renderTarget);
        //downsample(renderTarget, target);
        if(!window->actions.contains(Space))
            if(step()) window->render();
    }
} static test;
