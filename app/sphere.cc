#include "window.h"
#include "image-render.h"
#include "dng.h"
#include "math.h"
#include "algorithm.h"
#include "matrix.h"

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

static inline ImageF disk(const int halfSize, const bool negate=false, const float radius=0) {
    ImageF target = ImageF(uint2(2*halfSize));
    const float C = (2*halfSize-1.f)/2, R² = sq(radius?:C);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r² = sq(x-C)+sq(y-C);
        target(x,y) = float(negate^(r²<R²)); // FIXME: antialiasing
    }
    return target;
}

static inline ImageF circle(const int halfSize, const bool negate=false, const float radius=0) {
    ImageF target = ImageF(uint2(2*halfSize));
    const float C = (2*halfSize-1.f)/2, R = radius?:C;
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r = sqrt(sq(x-C)+sq(y-C));
        const float w = 1-::min(abs(r-R), 1.f);
        target(x,y) = negate?1-w:w;
    }
    return target;
}

inline double SSE(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 float SSE = 0;
 const float* a = A.data+aOffset.y*A.stride+aOffset.x;
 const float* b = B.data+bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const float* aLine = a+y*A.stride;
     const float* bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         SSE += sq(aLine[x] - bLine[x]);
         //SSE += abs(aLine[x] - bLine[x]);
         //if(aLine[x]) SSE -= bLine[x]; else SSE += bLine[x];
     }
 }
 return SSE;// / (size.x*size.y);
}

// Sum of dot product A·B with offset, windowed to valid samples
inline double SDP(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
 const int2 offset = centerOffset + (int2(A.size) - int2(B.size))/2;
 const uint2 aOffset (max(int2(0),+offset));
 const uint2 bOffset (max(int2(0),-offset));
 const uint2 size = min(A.size-aOffset, B.size-bOffset);
 float SSE = 0;
 const float* a = A.data+aOffset.y*A.stride+aOffset.x;
 const float* b = B.data+bOffset.y*B.stride+bOffset.x;
 for(uint y: range(size.y)) {
     const float* aLine = a+y*A.stride;
     const float* bLine = b+y*B.stride;
     for(uint x: range(size.x)) {
         SSE += aLine[x] * bLine[x];
         //SSE += sq(aLine[x] - bLine[x]);
         //SSE += abs(aLine[x] - bLine[x]);
         //if(aLine[x]) SSE -= bLine[x]; else SSE += bLine[x];
     }
 }
 return SSE;// / (size.x*size.y);
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

static offset_similarity argminSSE(const ImageF& A, const ImageF& B, int2 window=0_0, const int2 initialOffset=0_0) {
     return argmax(A.size, B.size, [&](const int2 offset){ return -SSE(A, B, offset); }, window, initialOffset);
}

static offset_similarity argmaxSDP(const ImageF& A, const ImageF& B, int2 window=0_0, const int2 initialOffset=0_0) {
     return argmax(A.size, B.size, [&](const int2 offset){ return SDP(A, B, offset); }, window, initialOffset);
}

template<Type F, Type... Images> int2 argmaxCoarse(const int L, F function, const Images&... images) {
    return ::argmax(function, downsample(images, L)...)*int(1<<L);
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

template<Type F, Type... Images> ImageF applyCoarse(const int L, F function, const Images&... images) {
    return upsample(::apply(function, downsample(images, L)...), L);
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

inline void multiply(const ImageF& Y, const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    assert_(Y.size == ::min(A.size, B.size));
    apply(A, B, centerOffset, [&](const uint y, const uint a, const uint b){ Y[y]=A[a]*B[b]; });
}
inline ImageF multiply(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    ImageF Y(::min(A.size, B.size));
    multiply(Y,A,B,centerOffset);
    return Y;
}

inline void extract(const ImageF& Y, const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    assert_(Y.size == ::min(A.size, B.size));
    apply(A, B, centerOffset, [&](const uint y, const uint, const uint b){ Y[y]=B[b]; });
}
inline ImageF extract(const ImageF& A, const ImageF& B, int2 centerOffset=0_0) {
    ImageF Y(::min(A.size, B.size));
    extract(Y,A,B,centerOffset);
    return Y;
}

inline void opGt(const ImageF& Y, const ImageF& X, float threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]>threshold); }
inline ImageF operator>(const ImageF& X, float threshold) { ImageF Y(X.size); ::opGt(Y,X,threshold); return Y; }

static inline vec4 principalCone(const ImageF& disk) {
    auto V = [&](uint ix, uint iy) {
            const float x = +((float(ix)/disk.size.x)*2-1);
            const float y = -((float(iy)/disk.size.y)*2-1);
            const float r² = sq(x)+sq(y);
            assert_(r² < 1);
            const float z = sqrt(1-r²);
            assert_(z > 0);
            const vec3 v = vec3(x,y,z);
            return v;
    };
    auto Ɐ = [&](function<void(float, float, vec3)> f) {
        for(uint iy: range(disk.size.y)) for(uint ix: range(disk.size.x)) { // ∫dA
            const float I = disk(ix, iy);
            if(I == 0) continue; // Mask
            const vec3 v = V(ix, iy);
            const float dΩ_dA = 1/v.z; // dΩ = sinθ dθ dφ, dA = r dr dφ, r=sinθ, dΩ/dA=dθ/dr=1/cosθ, cosθ=z
            f(dΩ_dA, I, v);
        }
    };
    float ΣN = 0, ΣⱯI = 0; Ɐ([&](float dΩ_dA, float I, vec3){ ΣN += dΩ_dA; ΣⱯI += dΩ_dA*I; });
    const float μI = ΣⱯI/ΣN;
    float ΣI = 0; vec3 ΣIv = 0_; Ɐ([&](float dΩ_dA, float I, vec3 v){ I-=μI; if(I>0) { ΣI += I; ΣIv += dΩ_dA*I*v; }});
    if(ΣI == 0) {
        const uint i = argmax(disk);
        return vec4(V(i%disk.size.x,i/disk.size.y), 0);
    }
    const vec3 μ = normalize(ΣIv); // Spherical mean of weighted directions
    const float p = length(ΣIv)/ΣI; // Polarisation
    const float Ω = p; // lim[p->0] Ω = p ; Ω=∫dΩ=2π[1-cosθ]~πθ², p=∫dΩv|z=π/2[1-cos2θ]~πθ²
    log(disk.size, disk.ref::size, ΣI, ΣIv, μ, p, Ω);
    return vec4(μ, Ω);
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
    //for(uint i: range(Y.ref::size)) Y[i] = abs(X[i]-x[i])/x[i];
    return Y;
}

static const int3 diskSearch(const ImageF& source, const int maxRadius/*, const uint L=1*/) {
#if 0
    const int L0 = 3;
    int2 offset = argminSSE(::disk(maxRadius>>L0, true), downsample(source, L0)).offset * (1<<L0);
#endif
#if 0
    int3 bestTransform (offset, maxRadius);
    //offset = argmaxSSE(::disk(maxRadius>>2, true), downsample(source, 2), int2(maxRadius>>2), offset).offset * (1<<2);
    //const ImageF image = L ? downsample(source, L) : unsafeShare(source);
    const ImageF image = L ? downsample(::DoG(source), L) : unsafeShare(::DoG(source));
    //const ImageF image = ::DoG(source);
    //const ImageF& image = source;
    float bestSimilarity = -inff;
    //for(int radius = maxRadius>>L; radius>8; radius-=8) {
    for(int radius = maxRadius>>L; radius>(maxRadius>>L)*2/3; radius-=2) {
        const auto R = argminSSE(::disk(radius, true, radius), image, int2(/*radius/8*/(maxRadius>>L)/4), offset/(1<<L));
        float similarity = R.similarity/sq(radius);
        log(radius, radius*(1<<L), R.similarity, offset, R.offset*(1<<L), similarity);
        if(similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestTransform = int3(R.offset, radius)*(1<<L);
        }
        //break;
        //offset = R.offset * (1<<L); // Iterative concurrent refinement of offset and radius
    }
#if 0
    {
        const int maxRadius = bestTransform.z;
        const ImageF& image = source;
        float bestSimilarity = -inff;
        for(int radius = maxRadius; radius>maxRadius-8; radius--) {
            const auto R = argmaxSSE(::disk(2*radius, true, radius), image, int2(/*radius/8*/(maxRadius>>L)/4), offset/(1<<L));
            log(radius, radius*(1<<L), R.similarity, offset, R.offset*(1<<L));
            if(R.similarity > bestSimilarity) {
                bestSimilarity = R.similarity;
                bestTransform = int3(R.offset, radius)*(1<<L);
            }
            //break;
            offset = R.offset * (1<<L); // Iterative concurrent refinement of offset and radius
        }
    }
#endif
    log(bestTransform, maxRadius);
#elif 0 // Break in texture (lower if replaces texture background or higher if replaces uniform background)
    //const ImageF DoG = ::DoG(source);
    //offset = argmaxSSE(::disk(maxRadius>>2, true), downsample(DoG, 2), int2(maxRadius>>2), offset).offset * (1<<2);
    int3 bestTransform (offset, maxRadius);
#elif 0
    int3 bestTransform (offset, maxRadius);
    //offset = argmaxSSE(::disk(maxRadius>>2, true), downsample(source, 2), int2(maxRadius>>2), offset).offset * (1<<2);
    //const ImageF image = L ? downsample(source, L) : unsafeShare(source);
    const ImageF image = (L ? downsample(::DoG(source), L) : ::DoG(source));// > 0.1f;
    //const ImageF image = ::DoG(source);
    //const ImageF& image = source;
    float bestSimilarity = 0;
    //for(int radius = maxRadius>>L; radius>8; radius-=8) {
    for(int radius = maxRadius>>L; radius>(maxRadius>>L)*2/3; radius-=4) {
        const auto R = argmaxSSQ(::circle(radius), image, int2(/*radius/8*/(maxRadius>>L)/2), offset/(1<<L));
        float similarity = R.similarity/radius;
        log(radius, radius*(1<<L), R.similarity, offset, R.offset*(1<<L), similarity);
        if(similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestTransform = int3(R.offset, radius)*(1<<L);
        }
        //break;
        offset = R.offset * (1<<L); // Iterative concurrent refinement of offset and radius
    }
#else
    const int L0 = 6;
    int2 offset = argmaxSDP(::disk(maxRadius>>L0), ::DoG(downsample(source, L0))).offset * (1<<L0); // Coarse
    const uint L1 = 1;
    const ImageF DoG1 = ::DoG(downsample(source, L1));
    offset = argmaxSDP(::disk(maxRadius>>1), DoG1, int2(maxRadius), offset/(1<<L1)).offset * (1<<L1); // Fine fixed radius
    int3 bestTransform (offset, maxRadius);
#if 0
    //const uint L = 1;
    float bestSimilarity = 0;
    //int3 bestTransform (offset, maxRadius);
    for(int radius = maxRadius>>L; radius>(maxRadius/2)>>L; radius--) {
        const auto R = argmaxSDP(::disk(radius>>L), DoG, int2(/*radius*/maxRadius>>L), offset/(1<<L)); // Fine fixed radius
        float similarity = R.similarity/sq(radius);
        log(radius, radius*(1<<L), R.similarity, offset, R.offset*(1<<L), similarity);
        if(similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestTransform = int3(R.offset, radius)*(1<<L);
        }
        //offset = R.offset * (1<<L);
    }
#elif 1
    const uint L = 0;
    float bestSimilarity = 0;
    const ImageF DoG = ::DoG(source);
    //int3 bestTransform (offset, maxRadius);
    for(int radius = maxRadius; radius>(maxRadius*2/3); radius-=2) {
        const auto R = argmaxSDP(::circle(radius), DoG, int2(radius/2), offset); // Fine fixed radius
        float similarity = R.similarity/radius;
        log(radius, radius, R.similarity, offset, R.offset, similarity);
        if(similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestTransform = int3(R.offset, radius)*(1<<L);
        }
        offset = R.offset;
    }
#endif
#endif
    log(bestTransform, maxRadius);
    return bestTransform;
}

inline float mean(const ref<float> v) { return sum(v, 0.)/v.size; }

struct Sphere : Widget {
    buffer<Image> preview;
    uint index = 0;
    unique<Window> window = nullptr;

    Sphere() {
        Time time {true};

        const CachedImageF source = loadRaw("IMG_0696.dng");
        const CachedImageF low = loadRaw("IMG_0698.dng"); // Low exposure (only highlights)

        const float μ = ::mean(source);
#if 0
        ImageF clip (source.size);
        for(uint i: range(source.ref::size)) clip[i] = ::min(source[i], μ);
        const ImageF& image = clip;
#else
        const ImageF& image = source;
#endif

        const int3 center = diskSearch(image, image.size.x/12);
        const ImageF templateDisk = ::disk(center[2]);

        //const ImageF DoG = ::DoG(image);// > 0.001f;
        const int L0 = 1;
        const ImageF DoG = upsample(downsample(::DoG(image),L0),L0);

        const ImageF disk = extract(templateDisk, DoG, center.xy());
        const ImageF light = multiply(templateDisk, low, center.xy());

        auto targets = {&light};
        //log(center, DoG.size);
        //auto targets = {&DoG};
        log(time);
#if 0
        const vec4 lightCone = principalCone(light > 0.7f);
        const vec3 μ = lightCone.xyz();
        const int2 μ_xy = int2((vec2(μ.x,-μ.y)+vec2(1))/vec2(2)*vec2(light.size));
        if(1) {
            const int r = 0;
            if(!anyGE(μ_xy+int2(r),int2(preview.size)) && !anyLE(μ_xy,int2(r)))
                for(int dy: range(-r,r+1))for(int dx: range(-r,r+1)) preview(μ_xy.x+dx, μ_xy.y+dy) = byte4(0,0xFF,0,0xFF);
        }

        const float dx = 5, f = 4;
        const vec3 C = normalize(vec3(vec2(center.x, -center.y) / float(image.size.x) * dx, f));
        const vec4 Q = rotationFromTo(C, vec3(0,0,1));
        const float Ω = lightCone.w, θ = acos(1-Ω/(2*π));
        log(C, μ, qapply(Q,μ), Ω, θ*180/π);
#endif
        preview = apply(ref<const ImageF*>(targets), [](const ImageF* image){ return sRGB(*image); });
        window = ::window(this, int2(preview[0].size), mainThread, 0);
        window->show();
        window->actions[Space] = [this](){ index=(index+1)%preview.size; window->render(); };
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview[index]);
    }
} static app;
