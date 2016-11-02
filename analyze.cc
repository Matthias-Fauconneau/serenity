#include "field.h"
#include "scene.h"
#include "math.h"
#include "parallel.h"

string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

struct LightFieldAnalyze : LightField {
    LightFieldAnalyze(Folder&& folder_ = Folder("/var/tmp/"_+basename(arguments()[0]))) : LightField(::move(folder_)) {
        if(!arguments().contains("coverage")) return;
        assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

        // Fits scene
        vec3 min = inff, max = -inff;
        Scene scene {::parseScene(readFile(basename(arguments()[0])+".scene"))};
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);

        Time time (true);
        const uint sSize = imageCount.x, tSize = imageCount.y;
        const uint uSize = imageSize.x, vSize = imageSize.y;

        const uint G = 1;
        uint3 gridSize (uSize/G+1, vSize/G+1, ::min(uSize,vSize)/G+1); // +1 to avoid boundary filtering issues but still keep aligned image and voxel grids
        assert_(imageCount.x * imageCount.y < 1<<16);
        assert_(gridSize.z*gridSize.y*gridSize.x < 1024*1024*1024);
        buffer<uint16> A (gridSize.z*gridSize.y*gridSize.x); // 2 GB
        A.clear(0); // Just to be sure™
        Lock Alock;

        buffer<uint8> hitVoxelss (threadCount()*gridSize.z*gridSize.y*gridSize.x/8); // 2 GB

        // Projects depth buffers to mark surfaces on voxel grid
        parallel_for(0, tSize*sSize, [this, sSize, gridSize, uSize, vSize, near, far, &Alock, &A, &hitVoxelss](uint threadID, uint stIndex) {
            mref<uint8> hitVoxels = hitVoxelss.slice(threadID*gridSize.z*gridSize.y*gridSize.x/8, gridSize.z*gridSize.y*gridSize.x/8);
            hitVoxels.clear(0);

            const uint2 imageCount = this->imageCount;
            const uint2 imageSize = this->imageSize;
            const float scale = (float)(imageSize.x-1)/(imageCount.x-1); // st -> uv
            const int size1 = this->size1;
            const int size2 = this->size2;
            const int size3 = this->size3;

            const int sIndex = stIndex%sSize, tIndex = stIndex/sSize;
            const vec2 st = vec2(sIndex, tIndex);

            const half* fieldZ = this->fieldZ.data;
            const half* Zst = fieldZ + (uint64)tIndex*size3 + sIndex*size2;

            const vec2 a = (scale*st + vec2(1)/*FIXME*/) * vec2(gridSize.xy()-uint2(2)) / (vec2(uSize,vSize)-vec2(1)) + vec2(1./2); // -2 to avoid boundary filtering issues
            const vec2 b = ((-2*far/(far-near))) * vec2(gridSize.xy()-uint2(2)) / (vec2(uSize,vSize)-vec2(1));
            const float c = - (far+near)/(far-near);
            const vec2 d = scale*st;

            for(const uint vIndex: range(vSize)) for(const uint uIndex: range(uSize)) {
                const vec2 uv = vec2(uIndex, vIndex);
                const float z = Zst[vIndex*size1 + uIndex]; // -1, 1
                const vec2 xy = a + b*(uv-d)/(z+c);
                if(xy.y < 0) continue;
                if(xy.x < 0) continue;
                const uint yIndex = xy.y;
                if(yIndex >= gridSize.y) continue;
                const uint xIndex = xy.x;
                if(xIndex >= gridSize.x) continue;

                const uint zIndex = ((z+1)/2)*(gridSize.z-2) + 1./2; // Perspective // -2 to avoid boundary filtering issues

                uint64 bitIndex = (zIndex*gridSize.y+yIndex)*gridSize.x+xIndex;
                //assert_(hitVoxels[bitIndex/8] & (1<<(bitIndex%8)) == 0, xIndex, yIndex, zIndex); // Maximum one hit per cell (pixels projects at least to full cell on near plane)
                hitVoxels[bitIndex/8] |= 1<<(bitIndex%8); // Scatter (TODO: atomic)
                // Marks hit voxels per view instead of incrementing global A buffer to count once per view
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

        size_t count = 0;
        for(const uint i: range(A.size)) if(A[i]) count++;
        log(gridSize, count, A.size, (float)count/A.size, (float)count/(imageSize.x*imageSize.y));

        // Render voxel grid to light field ...
        Folder folder("coverage", this->folder, true);
        for(string file: folder.list(Files)) remove(file, folder);
        File file (strx(uint2(sSize, tSize))+'x'+strx(uint2(uSize, vSize)), folder, Flags(ReadWrite|Create));
        file.resize(4ull*sSize*tSize*vSize*uSize*sizeof(half));
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);

        float maxA = ::max(A);
        //assert_(maxA == tSize*sSize, maxA, tSize*sSize);
        //assert_(maxA >= 15330 && maxA <= /*16641*/tSize*sSize, maxA, tSize*sSize);
        for(const uint stIndex: range(tSize*sSize)) {
            parallel_chunk(vSize, [this, stIndex, sSize, tSize, field, uSize, vSize, near, far, gridSize, &A, maxA](uint, uint start, uint sizeI) {
                const uint2 imageCount = this->imageCount;
                const uint2 imageSize = this->imageSize;
                const float scale = (float)(imageSize.x-1)/(imageCount.x-1); // st -> uv
                const int size1 = this->size1;
                const int size2 = this->size2;
                const int size3 = this->size3;

                const uint sIndex = (stIndex%sSize), tIndex = (stIndex/sSize);
                const vec2 st = vec2(sIndex, tIndex);

                const half* fieldZ = this->fieldZ.data;
                const half* Zst = fieldZ + (size_t)tIndex*size3 + sIndex*size2;

                mref<half> Z = field.slice(((0ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize);
                mref<half> B = field.slice(((1ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize);
                mref<half> G = field.slice(((2ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize);
                mref<half> R = field.slice(((3ull*tSize+tIndex)*sSize+sIndex)*vSize*uSize, vSize*uSize);

                const vec2 a = scale*st + vec2(1)/*FIXME*/; // Floor not round (no +1/2)
                const vec2 ba = (-2*far/(far-near));
                const vec2 bb = -ba*scale*st;
                const float c = - (far+near)/(far-near);

                for(const uint vIndex: range(start, start+sizeI)) for(const uint uIndex: range(uSize)) {
                    const size_t uvIndex = vIndex*size1 + uIndex;
                    const float z = Zst[uvIndex]; // -1, 1

                    const vec2 uv = vec2(uIndex, vIndex);
                    const vec2 b = ba*uv+bb;
                    const vec2 xy = a + b/(z+c);
                    if(xy.y < 0) continue;
                    if(xy.x < 0) continue;
                    const float yF = xy.y * (gridSize.y-2) / ((vSize-1)); // -2 to avoid boundary filtering issues
                    const uint yIndex = yF;
                    if(yIndex >= gridSize.y-1) continue;
                    const float xF = xy.x * (gridSize.x-2) / ((uSize-1)); // -2 to avoid boundary filtering issues
                    const uint xIndex = xF;
                    if(xIndex >= gridSize.x-1) continue;

                    const float zF = ((z+1)/2)*(gridSize.z-2); // -2 to avoid boundary filtering issues

#if 1 // Trilinear interpolation of non-zero samples
                    const float xf = fract(xF);
                    const float yf = fract(yF);
                    const float zf = fract(zF);
                    const uint zIndex = zF; // Perspective // Floor not round (no +1/2)
                    float Sw = 0, Sa = 0;
                    for(uint dz: range(2)) for(uint dy: range(2)) for(uint dx: range(2)) {
                        const uint16 a = A[((zIndex+dz)*gridSize.y+(yIndex+dy))*gridSize.x+(xIndex+dx)];
                        if(a) {
                            float w = (dz?zf:1-zf) * (dy?yf:1-yf) * (dx?xf:1-xf);
                            Sw += w;
                            Sa += w*a;
                        }
                    }
                    const float v = Sa/(Sw*maxA);
#elif 0
                    const float v = A[(uint(zF+1./2)*gridSize.y+uint(yF+1./2))*gridSize.x+uint(xF+1./2)]/maxA;
#else // Max
                    const uint zIndex = zF; // Perspective // Floor not round (no +1/2)
                    //assert(zIndex < gridSize.z-1); //if(zIndex >= gridSize.z-1) continue;
                    uint16 max = 0;
                    for(uint dz: range(2)) for(uint dy: range(2)) for(uint dx: range(2)) {
                        const uint16 a = A[((zIndex+dz)*gridSize.y+(yIndex+dy))*gridSize.x+(xIndex+dx)];
                        if(a > max) max = a;
                    }
                    const float v = max/maxA;
#endif
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
