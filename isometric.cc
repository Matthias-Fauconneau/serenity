#include "window.h"
#include "jpeg.h"

struct Isometric : Widget {
    const int N = 1024;
    ImageT<short2> extents = ImageT<short2>(N, N); // bottom, top Z position of each column
    buffer<byte3> voxels;

    float pitch = PI/6, yaw = PI/2;
    unique<Window> window = ::window(this, 0);

    Isometric() {
        short2 empty (0x7FFF,0x8000);
        extents.clear(empty);
        Map map ("6805_2520.las"_, home());
        struct Header {
            char signature[4]; //LASF
            uint16 source, encoding;
            uint32 project[4];
            uint8 version[2], system[32], software[32];
            uint16 day, year, size;
            uint32 data, recordCount;
            uint8 format;
            uint16 stride;
            uint32 count;
            uint32 countByReturn[5];
            double3 scale, offset;
            double maxX, minX, maxY, minY, maxZ, minZ;
        } packed &header = *(Header*)map.data;
        struct LASPoint {
            int3 position;
            uint16 intensity;
            uint8 returnNumber:3, returnCount:3, scanDirection:1, edgeOfFlightLine:1;
            uint8 classification, scaneAngleRank, userData; uint16 id;
            double time;
        } packed;
        double3 minD (header.minX, header.minY, header.minZ), maxD (header.maxX, header.maxY, header.maxZ);
        int3 minI ((minD-header.offset)/header.scale), maxI ((maxD-header.offset)/header.scale);
        buffer<int> histogram ((maxI.z-minI.z)*int(N-1)/(maxI.x-minI.x));
        histogram.clear(0);
        for(LASPoint point: cast<LASPoint>(map.slice(header.data)).slice(0, header.count)) {
            if(point.classification <= 1) continue;
            short3 p ((point.position-minI)*int(N-1)/(maxI.x-minI.x));
            auto& extent = extents(p.x,p.y);
            extent[0] = min(extent[0], p.z);
            extent[1] = max(extent[1], p.z);
            histogram[p.z]++;
        }
        int16 minClipZ = 0;
        for(int z: range(histogram.size)) {
            if(histogram[z] > histogram[minClipZ]) {
                minClipZ = z;
                int dz = 1;
                for(;z+dz<histogram.size;dz++) if(histogram[z+dz] < histogram[z+dz-1]) break;
                dz--;
                if(dz > 5) break;
            }
        }
        int16 maxClipZ = histogram.size-1;
        for(int z: reverse_range(histogram.size)) {
            if(histogram[z] > histogram[maxClipZ]) {
                maxClipZ = z;
                int dz = 1;
                for(;z-dz>=0;dz++) if(histogram[z-dz] < histogram[z-(dz-1)]) break;
                dz--;
                if(dz > 23) break;
            }
        }
        size_t voxelCount = 0;
        int16 minZ = 0x7FFF;
        for(int y: range(extents.size.y)) for(int x: range(extents.size.x)) {
            auto& extent = extents(x, y);
            {int16 neighbourMinZ = 0x7FFF;
            for(int Y: range(max(0, y-1), min(N, y+1+1))) for(int X: range(max(0, x-1), min(N, x+1+1)))
                if(Y != y || X != x) neighbourMinZ = min(neighbourMinZ, extents(X,Y)[0]);
            extent[0] = max(max(minClipZ, extent[0]), neighbourMinZ);}

            {int16 neighbourMaxZ = 0;
            for(int Y: range(max(0, y-1), min(N, y+1+1))) for(int X: range(max(0, x-1), min(N, x+1+1)))
                if(Y != y || X != x) neighbourMaxZ = max(neighbourMaxZ, extents(X,Y)[1]);
            extent[1] = min(min(maxClipZ, extent[1]), neighbourMaxZ);}

            if(extent[0] > extent[1]) { extent[1]=extent[0]-1; continue; }
            minZ = min(minZ, extent[0]);
            voxelCount += extent[1]+1-extent[0];
        }
        //log(columnCount, maxHeight, voxelCount, voxelCount/columnCount);
        voxels = buffer<byte3>(voxelCount); // ~ 20 M
        Image image = decodeJPEG(Map("6805_2520.jpg"_, home()));
        size_t voxelIndex = 0;
        for(int y: range(extents.size.y)) for(int x: range(extents.size.x)) {
            byte3 color = image(x*4, 4096-1-y*4).bgr();
            auto& extent = extents(x, y);
            extent[0] -= minZ, extent[1] -= minZ;
            for(int z = extent[0]; z<=extent[1]; z++, voxelIndex++) voxels[voxelIndex] = color; // TODO: detail (from intensity), allow concave
        }
    }

    vec2 sizeHint(vec2) { return vec2(0); }
    shared<Graphics> graphics(vec2 unused size) {
        const int W=size.x, H=size.y;
        Image target = Image(int2(size));
        target.clear(0);
        float Mxx = cos(yaw), Mxy = -sin(yaw);
        float Myx = -sin(pitch)*sin(yaw), Myy = -sin(pitch)*cos(yaw), Myz = -cos(pitch);
        size_t voxelIndex = 0;
        Image16 zBuffer (W, H); // implicit voxels indexing prevents alternative traversal orders
        zBuffer.clear(0);
        for(int y: range(extents.size.y)) for(int x: range(extents.size.x)) {
            //if(Myx > 0) x = N-1-x;
            //if(Myy > 0) y = N-1-y;
            auto& extent = extents(x, y);
            int X = W/2 + Mxx * (x-N/2) + Mxy * (y-N/2);
            float Y0 = H/2 + Myx * (x-N/2) + Myy * (y-N/2);
            for(int z = extent[0]; z<=extent[1]; z++, voxelIndex++) {
                int Y = Y0 + Myz * z;
                if(X >= 0 && Y>= 0 && Y < H && X < W && z > zBuffer(X, Y)) { zBuffer(X,Y) = z; target(X, Y) = voxels[voxelIndex]; }
            }
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        yaw += PI/120;
        window->render();
        return graphics;
    }
} view;
