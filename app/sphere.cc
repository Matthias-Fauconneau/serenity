#include "window.h"
#include "image-render.h"
#include "tiff.h"
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

static inline ImageF disk(int size) {
    ImageF target = ImageF(uint2(size));
    const float R = (size-1.f)/2, R² = sq(R);
    for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
        const float r² = sq(x-R)+sq(y-R);
        target(x,y) = float(r²<R²); // FIXME: antialiasing
    }
    return target;
}

static inline void negate(const ImageF& Y, const ImageF& X) { for(size_t i: range(Y.ref::size)) Y[i] = 1-X[i]; }
static inline ImageF negate(const ImageF& X) { ImageF Y(X.size); negate(Y,X); return Y; }

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
     }
 }
 return SSE;
}

generic int2 argmax(const uint2 Asize, const uint2 Bsize, T function, int2 window=0_0, const int2 initialOffset=0_0) {
    if(!window) window = abs(int2(Asize-Bsize)); // Full search
    int2 bestOffset = 0_0; float bestSimilarity = -inff; //-SSE..0
    for(int y: range(-window.y/2, window.y/2)) for(int x: range(-window.x/2, window.x/2)) {
        const int2 offset = initialOffset + int2(x, y);
        const float similarity = function(offset);
        if(similarity > bestSimilarity) { bestSimilarity = similarity; bestOffset = offset; }
    }
    return bestOffset;
}

template<Type F, Type... Args> int2 argmax(F function, const ImageF& A, const ImageF& B, const Args&... args) {
    return ::argmax(A.size, B.size, [&](const int2 offset){ return function(offset, A, B, args...); });
}

template<Type F, Type... Images> int2 argmaxCoarse(const int L, F function, const Images&... images) {
    return ::argmax(function, downsample(images, L)...)*int(1<<L);
}

static int2 argmaxSSE(const ImageF& A, const ImageF& B, const int L=0) {
     return argmaxCoarse(L, [&](const int2 offset, const ImageF& A, const ImageF& B){ return -SSE(A, B, offset); }, A, B);
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

inline void opEq(const ImageF& Y, const ImageF& X, float threshold) { for(uint i: range(Y.ref::size)) Y[i] = float(X[i]==threshold); }
inline ImageF operator==(const ImageF& X, float threshold) { ImageF Y(X.size); ::opEq(Y,X,threshold); return Y; }

static inline vec4 principalCone(const ImageF& disk) {
    float ΣI = 0; vec3 ΣIv = 0_;
    for(uint iy: range(disk.size.y)) for(uint ix: range(disk.size.x)) { // ∫dA
        const float x = +((float(ix)/disk.size.x)*2-1);
        const float y = -((float(iy)/disk.size.y)*2-1);
        const float r² = sq(x)+sq(y);
        const float z = sqrt(1-r²);
        if(z <= 0) continue;
        const float dΩ_dA = 1/z; // dΩ = sinθ dθ dφ, dA = r dr dφ, r=sinθ, dΩ/dA=dθ/dr=1/cosθ, cosθ=z
        const float I = disk(ix, iy);
        const float dΩ_dA_I = dΩ_dA*I; // dΩ/dA·I
        const vec3 v = vec3(x,y,z);
        ΣI += dΩ_dA_I;
        ΣIv += dΩ_dA_I*v;
    }
    const vec3 μ = normalize(ΣIv); // Spherical mean of weighted directions
    const float p = length(ΣIv)/ΣI; // Polarisation
    const float Ω = p; // lim[p->0] Ω = p ; Ω=∫dΩ=2π[1-cosθ]~πθ², p=∫dΩv|z=π/2[1-cos2θ]~πθ²
    return vec4(μ, Ω);
}

static Image grid(ref<Image> images) {
    const uint2 imageSize = images[0].size;
    Image target (uint2(images.size,1)*imageSize);
    for(int X: range(images.size)) {
        //assert_(images[X].size <= imageSize);
        copy(cropShare(target, int2(X*imageSize.x, 0), images[X].size), images[X]);
    }
    //ImageF target (uint2(1,images.size)*imageSize);
    //for(int Y: range(images.size)) copy(cropShare(target, int2(0, Y*imageSize.y), imageSize), images[Y]);
    return target;
}
template<Type... Images> Image grid(const Images&... images) { return grid(ref<Image>{unsafeShare(images)...}); }

// Sums CFA RGGB quads together, yields RGGB intensity image
static ImageF sumRGGB(const Image16& source) {
    ImageF target(source.size/2u);
    for(uint y: range(target.size.y)) for(uint x: range(target.size.x)) {
        target(x,y) = source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1);
    }
    return target;
}

struct Sphere : Widget {
    Time time {true};
    const ImageF image = sumRGGB(parseTIF(Map("IMG_0658.dng")));
    //const ImageF low = sumRGGB(parseTIF(Map("IMG_0659.dng"))); // Low exposure (only highlights)

    const ImageF templateDisk = ::disk(image.size.x/4);

    const int2 center = argmaxSSE(negate(templateDisk), image, 3);
    const ImageF disk = multiply(templateDisk, image, center);
    //const ImageF light = multiply(templateDisk, low, center);
    //const vec4 lightCone = principalCone(light);
    //Image preview = sRGB(light);
    Image preview = sRGB(disk);

    unique<Window> window = ::window(this, int2(preview.size), mainThread, 0);

    Sphere() {
        log(time);
#if 0
        const vec3 μ = lightCone.xyz();
        const int2 μ_xy = int2((vec2(μ.x,-μ.y)+vec2(1))/vec2(2)*vec2(disk.size));
        const int r = 1;
        //for(int dy: range(-r,r+1))for(int dx: range(-r,r+1)) preview(μ_xy.x+dx, μ_xy.y+dy) = byte4(0,0xFF,0,0xFF);

        const float dx = 5, f = 4;
        const vec3 C = normalize(vec3(vec2(center.x, -center.y) / float(image.size.x) * dx, f));
        const vec4 Q = rotationFromTo(C, vec3(0,0,1));
        const float Ω = lightCone.w, θ = acos(1-Ω/(2*π));
        log(C, μ, qapply(Q,μ), Ω, θ*180/π);
#endif
        window->show();
    }
    void render(RenderTarget2D& target_, vec2, vec2) override {
        const Image& target = (ImageRenderTarget&)target_;
        copy(target, preview);
    }
} static app;
