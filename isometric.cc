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
        buffer<int> histogram (maxI.z-minI.z);
        histogram.clear(0);
        for(LASPoint point: cast<LASPoint>(map.slice(header.data)).slice(0, header.count)) {
            if(point.classification <= 1) continue;
            short3 p ((point.position-minI)*int(N-1)/(maxI.x-minI.x));
            auto& extent = extents(p.x,p.y);
            extent[0] = min(extent[0], p.z);
            extent[1] = max(extent[1], p.z);
            histogram[p.z]++;
        }
        int clipZ = 0;
        for(size_t z: range(histogram.size)) {
            if(histogram[z] > histogram[clipZ]) {
                clipZ = z;
                int dz = 1;
                for(;z+dz<histogram.size;dz++) if(histogram[z+dz] < histogram[z+dz-1]) break;
                dz--;
                if(dz >= 7) break;
            }
        }
        log(clipZ, histogram[clipZ], histogram.size);
        size_t voxelCount = 0;
        for(auto& extent: extents) {
            extent[0] = max<int>(clipZ, extent[0]);
            if(extent[0] > extent[1]) { extent[1]=extent[0]-1; continue; }
            voxelCount += extent[1]+1-extent[0];
        }
        //log(columnCount, maxHeight, voxelCount, voxelCount/columnCount);
        voxels = buffer<byte3>(voxelCount); // ~ 20 M
        Image image = decodeJPEG(Map("6805_2520.jpg"_, home()));
        size_t voxelIndex = 0;
        for(int y: range(extents.size.y)) for(int x: range(extents.size.x)) {
            byte3 color = image(x*4, 4096-1-y*4).bgr();
            auto& extent = extents(x, y);
            for(int z = extent[0]; z<=extent[1]; z++, voxelIndex++) voxels[voxelIndex] = color;
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
        for(int y: range(extents.size.y)) for(int x: range(extents.size.x)) {
            auto& extent = extents(x, y);
            int X = W/2 + Mxx * (x-N/2) + Mxy * (y-N/2);
            float Y0 = H/2 + Myx * (x-N/2) + Myy * (y-N/2);
            for(int z = extent[0]; z<=extent[1]; z++, voxelIndex++) {
                int Y = Y0 + Myz * z;
                if(X >= 0 && Y>= 0 && Y < H && X < W) target(X, Y) = voxels[voxelIndex];
            }
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        yaw += PI/120;
        window->render();
        return graphics;
    }
} view;
