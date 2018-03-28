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
#include "render.h"
#include "encoder.h"

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
    Render scene;

    //Decoder video {"test.jpg"};
    Decoder video {"test.mp4"};
    Encoder output {"output.mp4"};
    unique<Window> window = nullptr;

    Image8 Y;
    ImageT</*bool*/float> R;

    array<uint2> Q;

    const float y = 210./297;
    const buffer<vec2> modelQ = copyRef(ref<vec2>{{-1,-y},{1,-y},{1,y},{-1,y}}); // FIXME: normalize origin and average distance ~ √2

    mat3 K;
    const float focalLength = 4.2, pixelPitch = 0.0014;

    mat4 Rt;

    uint frameIndex = 0;

    Test() {
        K(0,0) = 2/(video.size.x*pixelPitch/focalLength);
        K(1,1) = 2/(video.size.y*pixelPitch/focalLength);
        K(2,2) = 1;

        for(auto_: range(1)) step();
        //while(step()) {}
        window = ::window(this, int2(video.size/*/2u*/), mainThread, 0);
        window->show();
        //window->actions[Space] = [this]{ step(); window->render(); };

        output.setH264(video.size, video.videoTimeDen, video.videoTimeNum);
        //output.setH265(video.size, video.framePerSeconds);
        output.open();
    }

    bool step() {
        //log(frameIndex);
        frameIndex++;
        //const ImageF I = luminance(rotateHalfTurn(decodeImage(Map("test.jpg"))));
        Time time {true};
        if(!video.read()) return false;
        //const ImageF I = luminance(video.YUV(0));
        Y = video.YUV(0);
        //rotateHalfTurn(Y);
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

            for(;;) {
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
                sort(clusters);
                if(!changed) break;
            }
            threshold = ((clusters[K-3]+clusters[K-2])/2+clusters[K-1])/2;
        }

        // Floodfill outside to remove holes
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

        // Walk contour of main region (CCW)
        array<uint2> C (R.ref::size); // Contour
        uint2 start = R.size/2u;
        while(R(start+uint2(1,0))) start.x++; // Seed
        uint2 p = start;
        uint previousI = 0;
        for(;;) {
            //const int2 CCW[8] = {int2(-1,-1),int2(-1, 0),int2(-1,+1),int2( 0,+1),int2(+1,+1),int2(+1, 0),int2(+1,-1),int2( 0,-1)};
            const int2 CCW[8] = {int2(-1,-1),int2( 0,-1),int2(+1,-1),int2(+1, 0),int2(+1,+1),int2( 0,+1),int2(-1,+1),int2(-1, 0)};
            for(int i: range(8)) { // Searches for a CCW background->foreground transition
                const uint I = (previousI+5+i)%8; // Always start search from opposite direction ("concavest")
                const uint2 bg (int2(p)+CCW[I%8]);
                const uint2 fg (int2(p)+CCW[(I+1)%8]);
                if(R(bg)==0 && R(fg)==1) { // Assumes only one Bg->Fg transition (no holes)
                    assert_(C.size < C.capacity);
                    p = fg;
                    //if(i>=4) // FIXME
                    C.append(uint2(fg.x, R.size.y-1-fg.y)); // Flip Y axis from Y top down to Y bottom up
                    previousI = I;
                    break;
                }
            }
            if(p == start) break;
        }

        // Simplifies polygon to 4 corners
        /*array<uint2>*/ Q = copy(C);
        while(Q.size > 4) {
            float minA = inff; int bestI = -1;
            for(const uint i: range(Q.size)) {
                int2 p0 (Q[i]);
                int2 p1 (Q[(i+1)%Q.size]);
                int2 p2 (Q[(i+2)%Q.size]);
                int A = cross(p2-p0, p1-p0);
                if(A < minA) { minA = A; bestI = (i+1)%Q.size; }
            }
            Q.removeAt(bestI);
        }

        // Corner optimization (maximize total area)
        for(uint i: range(4)) {
            const uint2 Q3 = Q[(i+3)%4];
            uint2& Q0 = Q[(i+0)%4];
            const uint2 Q1 = Q[(i+1)%4];
            int A0 = cross(int2(Q1)-int2(Q0), int2(Q3)-int2(Q0));
            for(const uint2& c : C) {
                const int A = cross(int2(Q3)-int2(c), int2(Q1)-int2(c));
                if(A > A0) {
                    A0 = A;
                    Q0 = c;
                }
            }
        }

        // First edge is long edge (FIXME: preserve orientation across track)
        float lx = ::length(Q[1]-Q[0])+::length(Q[3]-Q[2]);
        float ly = ::length(Q[2]-Q[1])+::length(Q[0]-Q[3]);
        if(ly > lx) { uint2 q = Q[0]; Q.removeAt(0); Q.append(q); }
        //log("Corner", fmt(time.reset().milliseconds())+"ms"_);

        static constexpr uint N = 4;

        const mat3 K¯¹ = K.¯¹();
        const buffer<vec2> X´ = apply(Q, [&](uint2 q){ return K¯¹*(2.f*vec2(q)/vec2(R.size)-vec2(1)); });

        const ref<vec2> TX = modelQ;
        const ref<vec2> TX´ = X´;

        // DLT: Ah = 0
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
        const USV usv = SVD(A);
        const vector h = usv.V[usv.V.N-1];
        mat3 H;
        for(int i: range(usv.V.M)) H(i/3, i%3) = h[i];// / h[8];
        H = mat3(vec3(1/sqrt(::length(H[0])*::length(H[1])))) * H; // Normalizes by geometric mean of the 2 rotation vectors

        //mat4 Rt;
        Rt[0] = vec4(H[0], 0);
        Rt[1] = vec4(H[1], 0);
        Rt[2] = vec4(cross(H[0],H[1]), 0);
        Rt[3] = vec4(-H[2], 1); // Z-
#if 1
        Rt = Rt * mat4(vec4(-1,1,-1,1)); // Aligns direcion of scene Z+ to image bottom up
#endif
        Rt = mat4(vec4(-1,-1,1,1)) * Rt; // Flips X & Y as well to match Z- flip
        assert_(abs(1-((mat3)Rt).det())<0.09,((mat3)Rt).det(), abs(1-((mat3)Rt).det()));

        return true;
    }
    void render(const Image& target) {
        Time time {true};
        //target.clear(byte4(byte3(0),0xFF));
        for(uint i: range(target.ref::size)) target[i] = byte4(byte3(Y[i]), 0xFF); // FIXME

        //sRGB(target, R, 1);

        const mat3 flipY2 = mat3().translate(vec2(0, R.size.y-1)).scale(vec2(1, -1)); // Flips Y axis from Y bottom up to Y top down for ::line
        line(target, flipY2*vec2(Q[0]), flipY2*vec2(Q[1]), bgr3f(0,0,1));
        line(target, flipY2*vec2(Q[1]), flipY2*vec2(Q[2]), bgr3f(0,1,0));
        line(target, flipY2*vec2(Q[2]), flipY2*vec2(Q[3]), bgr3f(1,0,0));
        line(target, flipY2*vec2(Q[3]), flipY2*vec2(Q[0]), bgr3f(0,1,1));

        const float near = K(1,1);
        const float far = 1000/focalLength*near; //mm
        const mat4 projection = perspective(near, far).scale(vec3(float(R.size.y)/float(R.size.x), 1, 1));

        {
            const mat4 NDC = mat4()
                    .scale(vec3(vec2(R.size)/2.f, 1))
                    .translate(vec3(1)); // -1, 1 -> 0, 2
            const mat4 flipY = mat4().translate(vec3(0, R.size.y-1, 0)).scale(vec3(1, -1, 1)); // Flips Y axis from Y bottom up to Y top down for ::line
            const mat4 M = flipY*NDC*projection*Rt;

            line(target, (M*vec3(modelQ[0], 0)).xy(), (M*vec3(modelQ[1], 0)).xy(), bgr3f(0,0,1));
            line(target, (M*vec3(modelQ[1], 0)).xy(), (M*vec3(modelQ[2], 0)).xy(), bgr3f(0,1,0));
            line(target, (M*vec3(modelQ[2], 0)).xy(), (M*vec3(modelQ[3], 0)).xy(), bgr3f(1,0,0));
            line(target, (M*vec3(modelQ[3], 0)).xy(), (M*vec3(modelQ[0], 0)).xy(), bgr3f(0,1,1));

            const float z = 0.1;
            line(target, (M*vec3(modelQ[0], 0)).xy(), (M*vec3(modelQ[0], z)).xy(), bgr3f(1));
            line(target, (M*vec3(modelQ[1], 0)).xy(), (M*vec3(modelQ[1], z)).xy(), bgr3f(1));
            line(target, (M*vec3(modelQ[2], 0)).xy(), (M*vec3(modelQ[2], z)).xy(), bgr3f(1));
            line(target, (M*vec3(modelQ[3], 0)).xy(), (M*vec3(modelQ[3], z)).xy(), bgr3f(1));

            line(target, (M*vec3(modelQ[0], z)).xy(), (M*vec3(modelQ[1], z)).xy(), bgr3f(0,0,1));
            line(target, (M*vec3(modelQ[1], z)).xy(), (M*vec3(modelQ[2], z)).xy(), bgr3f(0,1,0));
            line(target, (M*vec3(modelQ[2], z)).xy(), (M*vec3(modelQ[3], z)).xy(), bgr3f(1,0,0));
            line(target, (M*vec3(modelQ[3], z)).xy(), (M*vec3(modelQ[0], z)).xy(), bgr3f(0,1,1));
        }

        const mat4 view = Rt * mat4().scale(vec3(1./2));
        scene.render(target, view, projection, Y);
        //log("Render", fmt(time.reset().milliseconds())+"ms"_);
    }
    void render(RenderTarget2D& renderTarget_, vec2, vec2) override {
        const Image& renderTarget = (ImageRenderTarget&)renderTarget_;
        render(renderTarget);
        Time time {true};
        output.writeVideoFrame(renderTarget);
        log("Encode", fmt(time.reset().milliseconds())+"ms"_);
        //downsample(renderTarget, target);
        if(!window->actions.contains(Space))
            if(step()) window->render();
    }
} static test;

