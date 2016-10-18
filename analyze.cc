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
        const size_t threadCount = ::threadCount();
        uint64 sums[threadCount];//, maxs[threadCount];
        mref<uint64>(sums, threadCount).clear(0);
        //mref<uint64>(maxs, threadCount).clear(0);
        const uint M = 1, N = 32;
        const uint Cx = (imageCount.x-1)/M+1, Cy = (imageCount.y-1)/M+1;
        const uint Sx = (imageSize.x-1)/N+1, Sy = (imageSize.y-1)/N+1;

        File file (strx(uint2(Cx, Cy))+'x'+strx(uint2(Sx, Sy)), Folder("coverage",currentWorkingDirectory(),true), Flags(ReadWrite|Create));
        file.resize(4*Cx*Cy*Sy*Sx*sizeof(half));
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);

        for(size_t stIndex: range(Cy*Cx)) {
            assert_(imageSize.x%2==0); // Gather 32bit / half
            parallel_chunk(Sy*Sx, [this, Cx, Cy, Sx, Sy, field, stIndex, &sums/*, &maxs*/](uint threadID, size_t start, size_t sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;

                const int sIndex = M*(stIndex%Cx), tIndex = M*(stIndex/Cx);
                const vec2 st = vec2(sIndex, tIndex);

                const half* fieldZ = this->fieldZ.data;
                const half* Zst = fieldZ + (size_t)tIndex*size3 + sIndex*size2;
                const v2si sample2D = {    0,           size1/2};
                const float zTolerance = 0x1p-2; // -5 ~N^?

                ImageH Z (unsafeRef(field.slice(((0ull*Cy+tIndex)*Cx+sIndex)*Sy*Sx, Sy*Sx)), uint2(Sx, Sy));
                ImageH B (unsafeRef(field.slice(((1ull*Cy+tIndex)*Cx+sIndex)*Sy*Sx, Sy*Sx)), uint2(Sx, Sy));
                ImageH G (unsafeRef(field.slice(((2ull*Cy+tIndex)*Cx+sIndex)*Sy*Sx, Sy*Sx)), uint2(Sx, Sy));
                ImageH R (unsafeRef(field.slice(((3ull*Cy+tIndex)*Cx+sIndex)*Sy*Sx, Sy*Sx)), uint2(Sx, Sy));

                uint64 sum = 0;//, max = 0;
                const uint64 fixedPoint = sq(Cx*Cy);

                for(size_t uvIndex: range(start, start+sizeI)) {
                    const int uIndex = N*(uvIndex%Sx), vIndex = N*(uvIndex/Sx);
                    const vec2 uv = vec2(uIndex, vIndex);
                    const float Zw = Zst[vIndex*size1 + uIndex];
                    const float z = Zw-1.f/2;

                    uint64 hits = 0;
                    for(size_t stIndex_: range(Cy*Cx)) {
                        const int sIndex_ = M*(stIndex_%Cx), tIndex_ = M*(stIndex_/Cx);
                        const vec2 st_ = vec2(sIndex_, tIndex_);
                        const vec2 uv_ = uv + scale * (st_ - st) * (-z) / (z+2);
                        if(uv_[0] < 0 || uv_[1] < 0) continue;
                        const int uIndex_ = uv_[0], vIndex_ = uv_[1];
                        if(uIndex_ > int(imageSize.x)-1 || vIndex_ > int(imageSize.y)-1) continue;
                        const size_t Zstuv_ = (size_t)tIndex_*size3 + sIndex_*size2 + vIndex_*size1 + uIndex_;
                        const v2sf x = {uv_[1], uv_[0]}; // vu
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        const v4sf w01uv = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                         * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
                        const v4sf Z_ = toFloat((v4hf)gather((float*)(fieldZ+Zstuv_), sample2D));
                        const float Zw_ = dot(w01uv, Z_);
                        /*if(sIndex==sIndex_ && tIndex==tIndex_) {
                            assert_(uv_==uv && uIndex==uIndex_ && vIndex==vIndex_);
                            assert_(abs(Zw - Zw_) < zTolerance, sIndex, tIndex, uIndex, vIndex, Zw, Zw_);
                        }*/
                        if(abs(Zw - Zw_) < zTolerance) hits++;
                        //else log("miss", sIndex, tIndex, sIndex_, tIndex_);
                    }
                    //assert_(hits, sIndex, vIndex, uIndex, vIndex, z); // Same view should hit at least
                    sum += fixedPoint/hits;
                    //max = ::max(max, hits);
                    Z(uvIndex%Sx, uvIndex/Sx) = Zw;
                    float v = (float)hits/(Cx*Cy); //1.f/hits;
                    B(uvIndex%Sx, uvIndex/Sx) = v;
                    G(uvIndex%Sx, uvIndex/Sx) = v;
                    R(uvIndex%Sx, uvIndex/Sx) = v;
                }
                sums[threadID] += sum;
                //maxs[threadID] = ::max(maxs[threadID], max);
            });
            if(stIndex%Cx==0) log(stIndex,"/",Cy*Cx);
        }
        const uint64 fixedPoint = sq(imageCount.x*imageCount.y);
        size_t sum = ::sum(ref<uint64>(sums,threadCount)) / fixedPoint;
        //size_t max = ::max(ref<uint64>(maxs,threadCount));
        log( (float)M*M*N*N*sum/(1024*1024),"MP", (float)M*M*N*N*sum/(imageSize.x*imageSize.y)/*, max*/); // "unique" pixel count (relative to full UV frame pixel count)
        log("Analyzed",strx(uint2(Cx, Cy)),"x",strx(uint2(Sx, Sy)),"images in", time);
#endif
    }
} analyze;
