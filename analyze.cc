#include "analyze.h"
#include "light.h"
#include "scene.h"
#include "math.h"
#include "parallel.h"

struct LightFieldAnalyze : LightField {
    LightFieldAnalyze(Folder&& folder_ = "."_) : LightField(endsWith(folder_.name(), "coverage")? Folder(folder_.name()+"/.."_) : ::move(folder_)) {
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
        const uint M = 1, N = 8;
        const uint sSize = (imageCount.x-1)/M+1, tSize = (imageCount.y-1)/M+1;
        const uint uSize = (imageSize.x-1)/N+1, vSize = (imageSize.y-1)/N+1;

        int3 gridSize (uSize, vSize, ::min(uSize,vSize)/8);
        buffer<uint16> A (gridSize.z*gridSize.y*gridSize.x); // 2 GB
        A.clear(0); // Just to be sure™

        for(size_t stIndex: range(tSize*sSize)) {
            assert_(imageSize.x%2==0); // Gather 32bit / half
            parallel_chunk(vSize*uSize, [/*this, Cx, Cy, uSize, Sy, field, stIndex,*/ &](uint unused threadID, size_t start, size_t sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;

                const int sIndex = M*(stIndex%sSize), tIndex = M*(stIndex/sSize);
                const vec2 st = vec2(sIndex, tIndex);

                const half* fieldZ = this->fieldZ.data;
                const half* Zst = fieldZ + (size_t)tIndex*size3 + sIndex*size2;
#if 1
                for(size_t uvIndex: range(start, start+sizeI)) {
                    const int uIndex = N*(uvIndex%uSize), vIndex = N*(uvIndex/uSize);
                    const vec2 uv = vec2(uIndex, vIndex);
                    //log(sIndex, tIndex, uIndex, vIndex);
                    const float Zw = Zst[vIndex*size1 + uIndex]; // -1, 1
                    const float a = - Zw / 2;
                    const int zIndex = (Zw+1)/2*(gridSize.z-1);
                    assert_(zIndex >= 0 && zIndex < gridSize.z);
                    const vec2 xy = a * (scale*st) + (1-a) * uv;
                    const int yIndex = xy.y * (gridSize.y-1)/(N*vSize-1);
                    if(!(yIndex >= 0 && yIndex < gridSize.y)) continue;
                    //assert_(yIndex >= 0 && yIndex < gridSize.y);
                    const int xIndex = xy.x * (gridSize.x-1)/(N*uSize-1);
                    if(!(xIndex >= 0 && xIndex < gridSize.x)) continue;
                    //assert_(xIndex >= 0 && xIndex < gridSize.x, sIndex, tIndex, uIndex, vIndex, ",", Zw, a, ",", xIndex, yIndex, zIndex);
                    A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex]++; // Scatter (TODO: atomic)
                }
            }, 1);
        }
        log("Analyzed",strx(uint2(sSize, tSize)),"x",strx(uint2(uSize, vSize)),"images in", time);
#if 1 // Render voxel grid to light field ...

        Folder folder("coverage", this->folder, true);
        for(string file: folder.list(Files)) remove(file, folder);
        File file (strx(uint2(sSize, tSize))+'x'+strx(uint2(uSize, vSize)), folder, Flags(ReadWrite|Create));
        file.resize(4*sSize*tSize*vSize*uSize*sizeof(half));
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        field.clear(0); // Just to be sure™
#if 1
        float maxA = ::max(A);
        log(maxA);
        for(size_t stIndex: range(tSize*sSize)) {
            parallel_chunk(vSize*uSize, [/*this, Cx, Cy, uSize, Sy, field, stIndex,*/ &](uint unused threadID, size_t start, size_t sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;

                const int sIndex = M*(stIndex%sSize), tIndex = M*(stIndex/sSize);
                const vec2 st = vec2(sIndex, tIndex);

                const half* fieldZ = this->fieldZ.data;
                const half* Zst = fieldZ + (size_t)tIndex*size3 + sIndex*size2;

                ImageH Z (unsafeRef(field.slice(((0ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize)), uint2(uSize, vSize));
                ImageH B (unsafeRef(field.slice(((1ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize)), uint2(uSize, vSize));
                ImageH G (unsafeRef(field.slice(((2ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize)), uint2(uSize, vSize));
                ImageH R (unsafeRef(field.slice(((3ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize)), uint2(uSize, vSize));

                for(size_t uvIndex: range(start, start+sizeI)) {
                    const int uIndex = N*(uvIndex%uSize), vIndex = N*(uvIndex/uSize);
                    const vec2 uv = vec2(uIndex, vIndex);
                    const float Zw = Zst[vIndex*size1 + uIndex]; // -1, 1
                    //assert_(Zw >= -1 && Zw <= 1, Zw);
                    const float a = - Zw / 2;
                    const int zIndex = (Zw+1)/2*(gridSize.z-1);
                    assert_(zIndex >= 0 && zIndex < gridSize.z);
                    const vec2 xy = a * (scale*st) + (1-a) * uv;
                    const int yIndex = xy.y * (gridSize.y-1)/(N*vSize-1);
                    if(!(yIndex >= 0 && yIndex < gridSize.y)) continue;
                    const int xIndex = xy.x * (gridSize.x-1)/(N*uSize-1);
                    if(!(xIndex >= 0 && xIndex < gridSize.x)) continue;
                    Z(uvIndex%uSize, uvIndex/uSize) = Zw;
                    //const float v = ::min(1.f, (float)A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex]/256);
                    //const float v = (float)A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex]/maxA;
                    //const float v = A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex] ? 1 : 0;
                    const float v = (1+Zw)/2;
                    B(uvIndex%uSize, uvIndex/uSize) = v;
                    G(uvIndex%uSize, uvIndex/uSize) = v;
                    R(uvIndex%uSize, uvIndex/uSize) = v;
                }
            }, 1);
        }
        log("Render", time);
#else
        for(size_t zIndex: range(gridSize.z)) for(size_t yIndex: range(gridSize.y)) for(size_t xIndex: range(gridSize.x)) {
            uint16 a = A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex];
            if(a==0) continue;
            for(size_t tIndex: range(tSize)) for(size_t sIndex: range(sSize)) {
                const float s = sIndex/float(sSize-1), t = tIndex/float(tSize-1);
                mat4 M = shearedPerspective(s, t);
                vec3
            }
        }
#endif
#endif
#else
                const v2si sample2D = {    0,           size1/2};
                const float zTolerance = 0x1p-5; // -5 ~N^?

                ImageH Z (unsafeRef(field.slice(((0ull*Cy+tIndex)*Cx+sIndex)*Sy*uSize, Sy*uSize)), uint2(uSize, Sy));
                ImageH B (unsafeRef(field.slice(((1ull*Cy+tIndex)*Cx+sIndex)*Sy*uSize, Sy*uSize)), uint2(uSize, Sy));
                ImageH G (unsafeRef(field.slice(((2ull*Cy+tIndex)*Cx+sIndex)*Sy*uSize, Sy*uSize)), uint2(uSize, Sy));
                ImageH R (unsafeRef(field.slice(((3ull*Cy+tIndex)*Cx+sIndex)*Sy*uSize, Sy*uSize)), uint2(uSize, Sy));

                uint64 sum = 0;
                const uint64 fixedPoint = sq(Cx*Cy);

                for(size_t uvIndex: range(start, start+sizeI)) {
                    const int uIndex = N*(uvIndex%uSize), vIndex = N*(uvIndex/uSize);
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
                        if(abs(Zw_ - Zw) < zTolerance) hits++;
                    }
                    sum += fixedPoint/hits;
                    Z(uvIndex%uSize, uvIndex/uSize) = Zw;
                    float v = (float)hits/(Cx*Cy);
                    B(uvIndex%uSize, uvIndex/uSize) = v;
                    G(uvIndex%uSize, uvIndex/uSize) = v;
                    R(uvIndex%uSize, uvIndex/uSize) = v;
                }
                sums[threadID] += sum;
            });
            if(stIndex%Cx==0) log(stIndex,"/",Cy*Cx);
        }
        const uint64 fixedPoint = sq(imageCount.x*imageCount.y);
        size_t sum = ::sum(ref<uint64>(sums,threadCount)) / fixedPoint;
        log( (float)M*M*N*N*sum/(1024*1024),"MP", (float)M*M*N*N*sum/(imageSize.x*imageSize.y)); // "unique" pixel count (relative to full UV frame pixel count)
        log("Analyzed",strx(uint2(Cx, Cy)),"x",strx(uint2(uSize, Sy)),"images in", time);
#endif
#endif
    }
} analyzeApp;
