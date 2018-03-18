#include "window.h"
#include "image-render.h"
#include "dng.h"
#include "math.h"
#include "algorithm.h"
#include "matrix.h"
#include "mwc.h"
#include "sort.h"

inline vec4 rotationFromTo(const vec3 v1, const vec3 v2) { return normalize(vec4(cross(v1, v2), sqrt(dotSq(v1)*dotSq(v2)) + dot(v1, v2))); }
inline vec4 qmul(vec4 p, vec4 q) { return vec4(p.w*q.xyz() + q.w*p.xyz() + cross(p.xyz(), q.xyz()), p.w*q.w - dot(p.xyz(), q.xyz())); }
inline vec3 qapply(vec4 p, vec3 v) { return qmul(p, qmul(vec4(v, 0), vec4(-p.xyz(), p.w))).xyz(); }

generic ImageT<T> downsample(const ImageT<T>& source, int times) {
    assert_(times>0);
    ImageT<T> target = unsafeShare(source);
    for(auto_: range(times)) target=downsample(target);
    return target;
}

generic ImageT<T> upsample(const ImageT<T>& source, int times) {
    assert_(times>0);
    ImageT<T> target = unsafeShare(source);
    for(auto_: range(times)) target=upsample(target);
    return target;
}

static inline ImageF disk(const int halfSize) {
    ImageF target = ImageF(uint2(2*halfSize));
    const float C = (2*halfSize-1.f)/2, R² = sq(C);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r² = sq(x-C)+sq(y-C);
        target(x,y) = float(r²<R²); // FIXME: antialiasing
    }
    return target;
}

static inline ImageF circle(const int halfSize) {
    ImageF target = ImageF(uint2(2*halfSize));
    const float C = (2*halfSize-1.f)/2, R = C;
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r = sqrt(sq(x-C)+sq(y-C));
        target(x,y) = 1-::min(abs(r-R), 1.f);
    }
    return target;
}

// Dot product A·B with offset, windowed to valid samples
inline double dot(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 const float* a = A.data+aOffset.y*A.stride+aOffset.x;
 const float* b = B.data+bOffset.y*B.stride+bOffset.x;
 float sum = 0;
 for(uint y: range(size.y)) {
     const float* aLine = a+y*A.stride;
     const float* bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         sum += aLine[x] * bLine[x];
     }
 }
 return sum;
}
struct offset_similarity { int2 offset; float similarity; };
generic offset_similarity argmax(const uint2 Asize, const uint2 Bsize, T function, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(Asize-Bsize)); // Full search
    int2 bestOffset = 0_0; float bestSimilarity = -inff; //-SSE..0
    for(int y: range(-window.y/2, window.y/2 +1)) for(int x: range(-window.x/2, window.x/2 +1)) {
        const int2 offset = initialOffset + int2(x, y);
        const float similarity = function(offset);
        if(similarity > bestSimilarity) { bestSimilarity = similarity; bestOffset = offset; }
    }
    return {bestOffset, bestSimilarity};
}

static offset_similarity argmaxDot(const ImageF& A, const ImageF& B, int2 window=0_0, const int2 initialOffset=0_0) {
     return argmax(A.size, B.size, [&](const int2 offset){ return dot(A, B, offset); }, window, initialOffset);
}

generic ImageF apply(const uint2 Asize, const uint2 Bsize, T function, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(Asize-Bsize)); // Full search
    ImageF target = ImageF( ::max(Asize, Bsize) );
    target.clear(-inff);
    for(int y: range(-window.y/2, (window.y+1)/2)) for(int x: range(-window.x/2, (window.x+1)/2))
        target(target.size.x/2+x, target.size.y/2+y) = function(initialOffset + int2(x, y));
    return ::move(target);
}

template<Type F, Type... Args> ImageF apply(F function, const ImageF& A, const ImageF& B, const Args&... args) {
    return ::apply(A.size, B.size, [&](const int2 offset){ return function(offset, A, B, args...); });
}

generic void apply(const ImageF& A, const ImageF& B, int2 centerOffset, T f) {
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

// Sums CFA RGGB quads together, and normalizes min/max levels, yields RGGB intensity image
static ImageF sumBGGR(const DNG& source) {
    ImageF target(source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        const int B = ::max(0, source(x*2+0,y*2+0)-source.blackLevel);
        const int G1 = ::max(0,source(x*2+1,y*2+0)-source.blackLevel);
        const int G2 = ::max(0,source(x*2+0,y*2+1)-source.blackLevel);
        const int R = ::max(0,source(x*2+1,y*2+1)-source.blackLevel);
        target(x,y) = float(B+G1+G2+R)/(4*(4095-source.blackLevel));
    }
    return target;
}

// Cached
struct CachedImageF : ImageF { Map map; };
static CachedImageF loadRaw(const string path) {
    if(!existsFile(path+".raw")) writeFile(path+".raw", cast<byte>(sumBGGR(parseDNG(Map(path), true))));
    DNG image = parseDNG(Map(path), false);
    Map map(path+".raw");
    return {ImageF(unsafeRef(cast<float>(map)), image.size/2u), ::move(map)};
}

static inline ImageF DoG(const ImageF& X) {
    const ImageF x = gaussianBlur(X,1);
    ImageF Y (X.size);
    for(uint i: range(Y.ref::size)) Y[i] = abs(X[i]-x[i]);
    return Y;
}

static const int3 diskSearch(const ImageF& source, const int maxRadius/*, const uint L=1*/) {
    const int L0 = 6;
    int2 offset = argmaxDot(::disk(maxRadius>>L0), ::DoG(downsample(source, L0))).offset * (1<<L0); // Coarse
    const uint L1 = 1;
    const ImageF DoG1 = ::DoG(downsample(source, L1));
    offset = argmaxDot(::disk(maxRadius>>1), DoG1, int2(maxRadius), offset/(1<<L1)).offset * (1<<L1); // Fine fixed radius
    // Radius search
    int3 bestTransform (offset, maxRadius);
    float bestSimilarity = 0;
    const ImageF DoG = ::DoG(source);
    auto test = [&](const int radius) {
        const ImageF circle = ::circle(radius);
        const auto R = argmaxDot(circle, DoG, int2(radius/3), offset); // Fine fixed radius
        float similarity = R.similarity/sum(circle);
        log(radius, radius, R.similarity, offset, R.offset, similarity);
        if(similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestTransform = int3(R.offset, radius);
        }
        offset = R.offset;
    };
    const int step = 3;
    for(int radius = maxRadius; radius>(maxRadius*2/3); radius-=step) test(radius);
    for(int radius = bestTransform.z+step-1; radius>bestTransform.z-step+1; radius--) test(radius);
    log(bestTransform);
    return bestTransform;
}

inline float mean(const ref<float> v) { return sum(v, 0.)/v.size; }

inline void multiply(const ImageF& Y, const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    assert_(Y.size == ::min(A.size, B.size));
    apply(A, B, centerOffset, [&](const uint y, const uint a, const uint b){ Y[y]=A[a]*B[b]; });
}
inline ImageF multiply(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    ImageF Y(::min(A.size, B.size));
    multiply(Y,A,B,centerOffset);
    return Y;
}

inline void opGt(const ImageF& Y, const ImageF& X, float threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]>threshold); }
inline ImageF operator>(const ImageF& X, float threshold) { ImageF Y(X.size); ::opGt(Y,X,threshold); return Y; }

static uint draw(Random& random, const ref<float> DPD, const float sum) {
    float u = random.next<float>()*sum;
    float p = 0;
    for(const uint i: range(DPD.size)) {
        p += DPD[i];
        if(p > u) return i;
    }
    error("");
}

// kmeans++
static inline buffer<vec4> principalLightCones(const ImageF& disk, const uint K) {
    buffer<vec4> samples (disk.ref::size, 0);
    for(uint iy: range(disk.size.y)) for(uint ix: range(disk.size.x)) {
        const float w = disk(ix, iy);
        if(w == 0) continue; // Mask
        const float x = +((float(ix)/disk.size.x)*2-1);
        const float y = -((float(iy)/disk.size.y)*2-1);
        const float r² = sq(x)+sq(y);
        if(!(r² <= 1)) continue;
        assert_(r² <= 1, ix, iy);
        const float z = sqrt(1-r²);
        assert_(z > 0);
        const vec3 N = vec3(x,y,z);
        const vec3 I = vec3(0,0,-1);
        const vec3 S = 2*dot(N,I)*N - I;
        assert_(abs(length(S)-1)<=0x1p-22, S, length(S), __builtin_log2(abs(1-length(S))));
        const float dΩ_dA = 1/N.z; // dΩ = sinθ dθ dφ, dA = r dr dφ, r=sinθ, dΩ/dA=dθ/dr=1/cosθ, cosθ=z
        samples.append(vec4(-S, dΩ_dA*w));
    }
    buffer<vec4> clusters (K);
    Random random;
    clusters[0] = samples[random.next<uint>()%samples.size];
#define Array(T, name, N) T name##_[N]; mref<T> name(name##_, N);
    for(int k: range(1, clusters.size)) {
        Array(float, DPD, samples.size); // FIXME: keep associated weight, FIXME: storing cDPD directly would allow to binary search in ::draw
        float sum = 0;
        for(const uint i: range(samples.size)) {
            float D = inff;
            for(vec4 cluster: clusters.slice(0,k)) D = ::min(D, 1-dot(samples[i].xyz(), cluster.xyz()));
            D *= samples[i].w;
            DPD[i] = D;
            sum += D;
        }
        const uint i = draw(random, DPD, sum);
        clusters[k] = samples[i];
    }

    for(auto_: range(1)) {
        typedef vec<xyz,double,3> vec3d;
        typedef vec<xyzw,double,4> vec4d;
        Array(vec4d, Σwv, clusters.size); Σwv.clear();
        for(const vec4& vw: samples) {
            float bestD = inff;
            uint k = 0;
            for(const uint ik: range(clusters.size)) {
                const float D = 1-dot(vw.xyz(), clusters[ik].xyz());
                if(D < bestD) {
                    bestD = D;
                    k = ik;
                }
            }
            assert_(abs(length(vw.xyz())-1)<=0x1p-22, vw.xyz(), length(vw.xyz()), __builtin_log2(abs(1-length(vw.xyz()))));
            Σwv[k] += vec4d(vec3d(vw.w*vw.xyz()), double(vw.w));
        }
        //log("Σwv", Σwv);
        for(const uint k: range(clusters.size)) {
            const vec3 μ = normalize(vec3(Σwv[k].xyz())); // Spherical mean of weighted directions
            const float p = length(Σwv[k].xyz())/Σwv[k].w; // Polarisation
            assert_(p < 1, p);
            const float Ω = 4*π*(1-p); // Ω=∫dΩ=2π[1-cosθ], Ωp=∫dΩv·μ=π/2[1-cos2θ]
            //log(μ, 2*acos(1-Ω/(2*π))*180/π);
            clusters[k] = vec4(μ, Ω);
        }
        //log("clusters", clusters);
    }
    return clusters;
 }

struct Sphere : Widget {
    buffer<Image> preview;
    uint index = 0;
    unique<Window> window = nullptr;

    Sphere() {
        Time time {true};
        const CachedImageF low = loadRaw("IMG_0711.dng"); // Low exposure (only highlights)
        const string name = "IMG_0710";
        if(!existsFile(name+".xyr")) {
            const CachedImageF image = loadRaw(name+".dng");
            const int3 center = diskSearch(image, image.size.x/12);
            writeFile(name+".xyr", raw(center));
        }
        const int3 center = raw<int3>(readFile(name+".xyr"));
        //log(center);
        const ImageF templateDisk = ::disk(center[2]);

        const ImageF light = multiply(templateDisk, low, center.xy());

        const ImageF direct (light.size);
        if(1) {
            const float threshold = sqrt(mean(light) * ::max(light));
            for(uint i: range(direct.ref::size)) direct[i] = light[i] >= threshold;
        } else {
            const float μ = mean(light);
            for(uint i: range(direct.ref::size)) direct[i] = ::max(0.f, light[i]-μ);
        }

        auto targets = {&direct};
        if(time.seconds()>0.1) log(time);

        const buffer<vec4> lightCones = principalLightCones(direct, 2);

        preview = apply(ref<const ImageF*>(targets), [](const ImageF* image){ return sRGB(*image); });

        for(const vec4& lightCone: lightCones) {
            const float dx = 5, f = 4;
            const vec3 C = normalize(vec3(vec2(center.x, -center.y) / float(low.size.x) * dx, f));
            const vec4 Q = rotationFromTo(C, vec3(0,0,1));
            const vec3 μ = lightCone.xyz();
            const float Ω = lightCone.w, θ = acos(1-Ω/(2*π));
            //const float d = 20, D = 220;
            //log(μ, Ω, θ, 2*θ*180/π, C, qapply(Q,μ), 2*atan(d, 2*D)*180/π, (atan(d, 2*D)-θ)*180/π);
            log(qapply(Q,μ), 2*θ*180/π);

            const vec2 μ_xy = (vec2(μ.x,-μ.y)+vec2(1))/vec2(2)*vec2(light.size);
            const int R = 3;
            for(int dy: range(-R,R+1))for(int dx: range(-R,R+1)) {
                const uint2 xy = uint2(int2(μ_xy)+int2(dx,dy));
                const float r = ::length(vec2(xy)+vec2(1./2)-μ_xy);
                if(r < R+1./2 && !anyGE(xy, preview[0].size)) blend(preview[0](xy), bgr3f(0,0,1), clamp(0.f,R+1.f/2-r,1.f));
            }
        }

        window = ::window(this, int2(preview[0].size), mainThread, 0);
        window->show();
        window->actions[Space] = [this](){ index=(index+1)%preview.size; window->render(); };
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview[index]);
    }
} static app;
