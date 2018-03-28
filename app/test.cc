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
inline int cross(int2 a, int2 b) { return a.x*b.y - a.y*b.x; }

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

        // K-means++ (FIXME: parameter K (=3))
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
        if(0) time.reset(); else log("Floodfill", fmt(time.reset().milliseconds())+"ms"_);

        uint2 start = R.size/2u;
        while(R(start+uint2(1,0))) start.x++;
        array<uint2> H (R.ref::size); // Hull
        uint2 p = start;
        uint previousI = 0;
        //H.append(start);
        for(;;) { // Walk CCW
            const int2 CCW[8] = {int2(-1,-1),int2(-1, 0),int2(-1,+1),int2( 0,+1),int2(+1,+1),int2(+1, 0),int2(+1,-1),int2( 0,-1)};
            //const uint previousSize = H.size;
            for(int i: range(8)) { // Searches for a CCW background->foreground transition
                const uint I = (previousI+5+i)%8; // Always start search from opposite direction ("concavest")
                const uint2 bg (int2(p)+CCW[I%8]);
                const uint2 fg (int2(p)+CCW[(I+1)%8]);
                if(R(bg)==0 && R(fg)==1) { // Assumes only one Bg->Fg transition (no holes)
                    assert_(H.size < H.capacity);
                    p = fg;
                    //if(H.size >= 2 && cross(int2(H[H.size-1])-int2(H[H.size-2]), int2(p)-int2(H[H.size-1])) <= 0) H.last() = fg;
                    //else H.append(fg);
                    //if(i>=4) // FIXME
                    H.append(uint2(fg.x, R.size.y-1-fg.y)); // Flip Y axis from Y top down to Y bottom up
                    previousI = I;
                    break;
                }
            }
            if(p == start) break;
        }

        //R = ImageT</*bool*/float>(Y.size);
        for(float& r: R) r /= 8;

        auto line = [this](vec2 p0, vec2 p1) {
            if(anyGE(uint2(p0), R.size)) return;
            if(anyGE(uint2(p1), R.size)) return;
            float dx = p1.x - p0.x, dy = p1.y - p0.y;
            bool transpose=false;
            if(abs(dx) < abs(dy)) { swap(p0.x, p0.y); swap(p1.x, p1.y); swap(dx, dy); transpose=true; }
            if(p0.x > p1.x) { swap(p0.x, p1.x); swap(p0.y, p1.y); }
            const float gradient = dy / dx;
            float intery = p0.y + gradient * (round(p0.x) - p0.x) + gradient;
            for(int x: range(p0.x, p1.x +1)) {
                (transpose ? R(intery, x) : R(x, intery)) = 1;
                intery += gradient;
            }
        };

#if 0
        // Fit OBB
        float minA = inff; typedef vec2 OBB[4]; OBB obb;
        for(const uint i: range(H.size)) {
            const vec2 e0 = normalize(vec2(H[(i+1)%H.size])-vec2(H[i]));
            const vec2 e1 = normal(e0);
            vec2 min=vec2(inff), max=vec2(-inff);
            for(uint2 h: H) {
                min.x = ::min(min.x, dot(e0, vec2(h)));
                max.x = ::max(max.x, dot(e0, vec2(h)));
                min.y = ::min(min.y, dot(e1, vec2(h)));
                max.y = ::max(max.y, dot(e1, vec2(h)));
            }
            float A = dotSq(max-min);
            if(A < minA) {
                obb[0] = mat2(e0, e1) * vec2(min.x, min.y);
                obb[1] = mat2(e0, e1) * vec2(max.x, min.y);
                obb[2] = mat2(e0, e1) * vec2(max.x, max.y);
                obb[3] = mat2(e0, e1) * vec2(min.x, max.y);
            }
        }
        for(uint i: range(4)) line(obb[i], obb[(i+1)%4]);
#endif

        // Simplifies polygon to 4 corners
        array<uint2> C = copy(H);
        while(C.size > 4) {
            float minA = inff; int bestI = -1;
            for(const uint i: range(C.size)) {
                int2 p0 (C[i]);
                int2 p1 (C[(i+1)%C.size]);
                int2 p2 (C[(i+2)%C.size]);
                int A = cross(p1-p0, p2-p0);
                if(A < minA) { minA = A; bestI = (i+1)%C.size; }
            }
            C.removeAt(bestI);
        }

        // Corner optimization (maximize total area)
        for(uint i: range(4)) {
            const uint2 C3 = C[(i+3)%4];
            uint2& C0 = C[(i+0)%4];
            const uint2 C1 = C[(i+1)%4];
            int A0 = cross(int2(C1)-int2(C0), int2(C3)-int2(C0));
            for(const uint2& h : H) {
                const int A = cross(int2(C1)-int2(h), int2(C3)-int2(h));
                if(A > A0) {
                    A0 = A;
                    C0 = h;
                }
            }
        }

        //for(uint i: range(H.size)) line(vec2(H[i]), vec2(H[(i+1)%H.size]));
        const mat3 flipY2 = mat3().translate(vec2(0, R.size.y-1)).scale(vec2(1, -1)); // Flips Y axis from Y bottom up to Y top down for ::line
        for(uint i: range(C.size)) line(flipY2*vec2(C[i]), flipY2*vec2(C[(i+1)%C.size]));

        //log("Corner", fmt(time.reset().milliseconds())+"ms"_);
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
