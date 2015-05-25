#include "window.h"

struct Isometric : Widget {
    unique<Window> window = ::window(this, 0);

    buffer<short4> points;
    short4 min = -1, mean, max = 0;
    Isometric() {
        Map map ("6805_2520.las"_, home());
        struct Header {
            char signature[4]; //LASF
            uint16 source, encoding;
            uint32 project[4];
            uint8 version[2], system[32], software[32];
            uint16 day, year, size;
            uint32 data, variableRecordCount;
            uint8 format;
            uint16 stride;
            uint32 count;
            uint32 countByReturn[5];
            double3 scale, offset;
            double maxX, minX, maxY, minY, maxZ, minZ;
        } packed &header = *(Header*)map.data;
        assert(header.format==1);
        double3 minD (header.minX, header.minY, header.minZ);
        double3 maxD (header.maxX, header.maxY, header.maxZ);
        struct Point {
            int3 position;
            uint16 intensity;
            uint8 returnNumber:3, returnCount:3, scanDirection:1, edgeOfFlightLine:1;
            uint8 classification, scaneAngleRank, userData; uint16 id;
            double time;
        } packed;
        assert_(sizeof(Point)==header.stride, sizeof(Point), header.stride);
        points = buffer<short4>(header.count, 0);
        int3 minI ((minD-header.offset)/header.scale), maxI ((maxD-header.offset)/header.scale);
        int scale = (maxI.x-minI.x) / 2048;
        long4 sum;
        for(Point p: cast<Point>(map.slice(header.data)).slice(0, header.count)) {
            short4 point(short3((p.position-minI)/scale), ::min(0xFF, int(p.intensity)));
            min = ::min(min, point);
            max = ::max(max, point);
            sum += long4(point);
            points.append(point);
        }
        mean = short4(sum / int64(points.size));
        for(short4& p: points) p -= short4(mean.x, mean.y, mean.z, 0);
        log(min, mean, max, header.count, log2(float(header.count)), points.size, log2(float(points.size)));
    }

    vec2 sizeHint(vec2) { return vec2(0); }
    float pitch = PI/6, yaw = PI/2;
    shared<Graphics> graphics(vec2 unused size) {
        const int W=size.x, H=size.y;
        Image target = Image(int2(size));
        target.clear(0);
        float Mxx = cos(yaw), Mxy = -sin(yaw);
        float Myx = -sin(pitch)*sin(yaw), Myy = -sin(pitch)*cos(yaw), Myz = -cos(pitch);
        for(short4 p: points) {
            int x = p.x, y = p.y, z = p.z;
            int X = W/2 + Mxx * x + Mxy * y;
            int Y = H/2 + Myx * x + Myy * y + Myz * z;
            if(X >= 0 && Y>= 0 && Y < H && X < W) target(X, Y) = p.w;
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        yaw += PI/120;
        window->render();
        return graphics;
    }
} view;
