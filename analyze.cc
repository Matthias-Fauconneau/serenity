#include "field.h"
#include "scene.h"
#include "math.h"
#include "parallel.h"

struct LightFieldAnalyze : LightField {
    LightFieldAnalyze(Folder&& folder_ = "."_) : LightField(endsWith(folder_.name(), "coverage")? Folder(folder_.name()+"/.."_) : ::move(folder_)) {
        if(!endsWith(folder_.name(), "coverage")) return;
        assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

        // Fits scene
        vec3 min = inff, max = -inff;
        Scene scene {::parseScene(readFile("box.scene",home()))};
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        Time time (true);
        const uint M = 1, N = 2;
        const uint sSize = (imageCount.x-1)/M+1, tSize = (imageCount.y-1)/M+1;
        const uint uSize = (imageSize.x-1)/N+1, vSize = (imageSize.y-1)/N+1;

        uint3 gridSize (uSize/16, vSize/16, ::min(uSize,vSize)/16);
        buffer<uint16> A (gridSize.z*gridSize.y*gridSize.x); // 2 GB
        A.clear(0); // Just to be sure™
        Lock Alock;

        buffer<uint8> hitVoxelss (threadCount()*gridSize.z*gridSize.y*gridSize.x/8); // 2 GB

        parallel_for(0, tSize*sSize, [this, sSize, gridSize, uSize, vSize, near, far, &Alock, &A, &hitVoxelss](uint threadID, uint stIndex) {
            mref<uint8> hitVoxels = hitVoxelss.slice(threadID*gridSize.z*gridSize.y*gridSize.x/8, gridSize.z*gridSize.y*gridSize.x/8);
            hitVoxels.clear(0);

            const uint2 imageCount = this->imageCount;
            const uint2 imageSize = this->imageSize;
            const float scale = (float)(imageSize.x-1)/(imageCount.x-1); // st -> uv
            const int size1 = this->size1;
            const int size2 = this->size2;
            const int size3 = this->size3;

            const int sIndex = M*(stIndex%sSize), tIndex = M*(stIndex/sSize);
            const vec2 st = vec2(sIndex, tIndex);

            const half* fieldZ = this->fieldZ.data;
            const half* Zst = fieldZ + (uint64)tIndex*size3 + sIndex*size2;

            for(const uint vIndex_: range(vSize)) for(const uint uIndex_: range(uSize)) {
                const uint vIndex = N*vIndex_, uIndex = N*uIndex_;

                const vec2 uv = vec2(uIndex, vIndex);
                const float z = Zst[vIndex*size1 + uIndex]; // -1, 1
                const float a = ((- 2*far*near / ((far-near)*z - (far+near)))-near)/(far-near); // Linear
                const vec2 xy = (1-a) * (scale*st) + a * uv;
                if(xy.y < 0) continue;
                if(xy.x < 0) continue;
                const uint yIndex = xy.y * (gridSize.y-1) / (N*(vSize-1));
                if(yIndex >= gridSize.y) continue;
                const uint xIndex = xy.x * (gridSize.x-1) / (N*(uSize-1));
                if(xIndex >= gridSize.x) continue;

                const uint zIndex = ((z+1)/2)*(gridSize.z-1); // Perspective

                uint64 bitIndex = (zIndex*gridSize.y+yIndex)*gridSize.x+xIndex;
                hitVoxels[bitIndex/8] |= 1<<(bitIndex%8); // Scatter (TODO: atomic)
                // Mark hit voxels per view instead of incrementing global A buffer to count once per view
                // i.e bin hit with different uv coordinates from same view (st) is incremented once
            }

            Locker lock(Alock);
            for(const uint byteIndex: range(hitVoxels.size)) {
                for(const uint bit: range(8)) {
                    if(hitVoxels[byteIndex] & (1<<bit)) A[byteIndex*8+bit]++;
                }
            }
        });
        log("Analyzed",strx(uint2(sSize, tSize)),"x",strx(uint2(uSize, vSize)),"images in", time.reset());

        // Render voxel grid to light field ...
        Folder folder("coverage", this->folder, true);
        for(string file: folder.list(Files)) remove(file, folder);
        File file (strx(uint2(sSize, tSize))+'x'+strx(uint2(uSize, vSize)), folder, Flags(ReadWrite|Create));
        file.resize(4*sSize*tSize*vSize*uSize*sizeof(half));
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);

        float maxA = ::max(A);
        assert_(maxA == tSize*sSize);
        for(const uint stIndex: range(tSize*sSize)) {
            parallel_chunk(vSize*uSize, [this, stIndex, sSize, tSize, field, uSize, vSize, gridSize, &A, maxA](uint, uint start, uint sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float)(imageSize.x-1)/(imageCount.x-1); // st -> uv
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

                for(const uint uvIndex: range(start, start+sizeI)) {
                    const uint uIndex = N*(uvIndex%uSize), vIndex = N*(uvIndex/uSize);

                    const vec2 uv = vec2(uIndex, vIndex);
                    const float z = Zst[vIndex*size1 + uIndex]; // far, uv, near, st = 3/2, 1/2, -1/2, -1/2
                    const float a = 1./2*z + 3./4; // st, uv = 0, 1
                    const vec2 xy = (1-a) * (scale*st) + a * uv;
                    if(xy.y < 0) continue;
                    if(xy.x < 0) continue;
                    const uint yIndex = xy.y * (gridSize.y-1) / (N*(vSize-1));
                    if(yIndex >= gridSize.y) continue;
                    const uint xIndex = xy.x * (gridSize.x-1) / (N*(uSize-1));
                    if(xIndex >= gridSize.x) continue;

                    const float w = float(1./2)*z + float(3./4);
                    const float ZoverW = z/w; // -1 .. 1
                    const uint zIndex = (ZoverW+1)/2*(gridSize.z-1);

                    const float v = (float)A[(zIndex*gridSize.y+yIndex)*gridSize.x+xIndex]/maxA;
                    //const float v = (xIndex+yIndex+zIndex)%2;
                    Z[uvIndex] = z;
                    B[uvIndex] = v;
                    G[uvIndex] = v;
                    R[uvIndex] = v;
                }
            });
        }
        log("Render", time);
    }
} analyzeApp;
