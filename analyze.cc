#include "light.h"
#include "scene.h"
#include "math.h"
#include "parallel.h"

struct LightFieldAnalyze : LightField {
    LightFieldAnalyze() {
#if 0
        float sum = 0;
        for(const Scene::Face& face: Scene().faces) {
            const vec3 a = face.position[0], b = face.position[1], c = face.position[2];
            const vec3 N = cross(b-a,c-a); // Face normal
            vec2 v = clamp(vec2(-2), vec2(N.x?N.x/N.z:0, N.y?N.y/N.z:0), vec2(2)); // Closest view vector (normalized by Z)
            const float PSA = dot(normalize(vec3(v,1)), N) / 2; // Projected triangle surface area
            log(N, v, PSA);
            if(PSA<0) continue; // Backward face culling
            sum += PSA;
        }
        log(sum);
#else
        assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

        Time time (true);
        const size_t threadCount = 1; //::threadCount();
        uint64 sums[threadCount];
        mref<uint64>(sums, threadCount).clear(0);
        //parallel_for(0, imageCount.x*imageCount.y, [this, &sums](uint threadID, size_t stIndex) {{
        const uint M = 1, N = 16;
        const uint Cx = (imageCount.x-1)/M+1, Cy = (imageCount.y-1)/M+1;
        const uint Sx = (imageSize.x-1)/N+1, Sy = (imageSize.y-1)/M+1;
        ({ const uint threadID = 0; for(size_t stIndex: range(Cy*Cx)) {
            //uint64 sum = 0;

            assert_(imageSize.x%2==0); // Gather 32bit / half
            //for(size_t uvIndex: range(imageSize.x*imageSize.y)) {
            parallel_chunk(Sy*Sx, [this, Cx, Cy, stIndex, &sums](uint, size_t start, size_t sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;
                const int sIndex = M*(stIndex%Cx), tIndex = M*(stIndex/Cx);
                const vec2 st = vec2(sIndex, tIndex);
                const half* Zst = fieldZ.data + (size_t)tIndex*size3 + sIndex*size2;
                const v2si sample2D = {    0,           size1/2};

                uint64 sum = 0;
                const uint64 fixedPoint = sq(imageCount.x*imageCount.y);

                for(size_t uvIndex: range(start, start+sizeI)) {
                    int uIndex = N*(uvIndex%Cx), vIndex = N*(uvIndex/Cx);
                    const vec2 uv = vec2(uIndex, vIndex);
                    const float Zw = Zst[vIndex*size1 + uIndex];
                    const float z = Zw-1.f/2;

                    size_t hits = 0;
                    /*for(size_t stIndex_: range((imageCount.x*imageCount.y)) {
                        const int sIndex_ = stIndex_%imageCount.x, tIndex_ = stIndex_/imageCount.x;*/
                        {const int sIndex_ = imageCount.x/2, tIndex_ = imageCount.y/2;
                        const vec2 st_ = vec2(sIndex_, tIndex_);
                        const vec2 uv_ = uv + scale * (st_ - st) * (-z) / (z+2);
                        if(uv_[0] < 0 || uv_[1] < 0) continue;
                        const int uIndex_ = uv_[0], vIndex_ = uv_[1];
                        if(uIndex_ >= int(imageSize.x)-1 || vIndex_ >= int(imageSize.y)-1) continue;
                        const size_t Zstuv_ = (size_t)tIndex_*size3 + sIndex_*size2 + vIndex_*size1 + uIndex_;
                        const v2sf x = {uv_[1], uv_[0]}; // vu
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        const v4sf w01uv = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
                        const v4sf Z_ = toFloat((v4hf)gather((float*)(fieldZ.data+Zstuv_), sample2D));
                        const float z_ = dot(w01uv, Z_);
                        if(abs(z - z_) < 0x1p-5) hits++;
                    }
                    if(hits) sum += fixedPoint/hits;
                }
                sums[threadID] += sum;
            });
            //sums[threadID] += sum;
        }});
        const uint64 fixedPoint = sq(imageCount.x*imageCount.y);
        size_t sum = ::sum(ref<uint64>(sums,threadCount)) / fixedPoint;
        log( (float)M*M*N*N*sum/(1024*1024),"MP", (float)M*M*N*N*sum/(imageSize.x*imageSize.y) ); // "unique" pixel count (relative to full UV frame pixel count)
        log("Analyzed",strx(imageCount),"x",strx(imageSize),"images in", time);
#endif
    }
} analyze;
