#include "window.h"
#include "jpeg.h"

struct Isometric : Widget {
    unique<Window> window = ::window(this, 0);

    struct Point { // TODO: 8bit tiles
        short3 position;
        byte3 color;
    };
    buffer<Point> points;
    short4 min = -1, mean, max = 0;
    Isometric() {
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
        //assert(header.version=={1, 2} && header.format==1);
        double3 minD (header.minX, header.minY, header.minZ);
        double3 maxD (header.maxX, header.maxY, header.maxZ);
        ref<byte> s = map.slice(sizeof(Header));
        for(uint unused i: range(header.recordCount)) {
            struct Header {
                uint16 reserved;
                char user[16];
                uint16 record;
                uint16 size;
                char description[16];
            } header = *(Header*)s.data;
            log(header.record, header.description);
            s = s.slice(sizeof(Header)+header.size);
        }
        struct LASPoint {
            int3 position;
            uint16 intensity;
            uint8 returnNumber:3, returnCount:3, scanDirection:1, edgeOfFlightLine:1;
            uint8 classification, scaneAngleRank, userData; uint16 id;
            double time;
        } packed;
        assert_(sizeof(LASPoint)==header.stride, sizeof(LASPoint), header.stride);
        points = buffer<Point>(header.count, 0);
        log(minD, maxD);
        int3 minI ((minD-header.offset)/header.scale), maxI ((maxD-header.offset)/header.scale);
        //int scale = (maxI.x-minI.x) / 2048;
        long4 sum;
        enum { Unclassified, Undefined, Ground, LowVegetation, Vegetation, HighVegetation, Building, Noise, Model, Water, Reserved1, Reserved2, Overlap };
        Image image = decodeJPEG(Map("6805_2520.jpg"_, home()));
        for(LASPoint p: cast<LASPoint>(map.slice(header.data)).slice(0, header.count)) {
            if(p.classification == Noise || p.classification == Overlap) continue;
            short3 position((p.position-minI)*2047/(maxI.x-minI.x));
            short4 point(position, ::min(0xFF, int(p.intensity)));
            min = ::min(min, point);
            max = ::max(max, point);
            sum += long4(point);
            /*uint16 colorIndex;
            if(p.classification == Ground) colorIndex = 0;
            else if(p.classification == LowVegetation) colorIndex = 1;
            else if(p.classification == Vegetation) colorIndex = 2;
            else if(p.classification == HighVegetation) colorIndex = 3;
            else if(p.classification == Building) colorIndex = 4;
            else colorIndex = 5; //error(withName(p.classification));*/
            assert_(position.x >= 0 && position.x < 2048 && position.y >= 0 && position.y < 2048, position);
            byte4 color = image(position.y*2, 4095-position.x*2);
            points.append({position, byte3(color.b, color.g, color.r)});
        }
        mean = short4(sum / int64(points.size));
        for(Point& p: points) p.position -= short3(mean.x, mean.y, mean.z);
        //log(min, mean, max, header.count, log2(float(header.count)), points.size, log2(float(points.size)));
    }

    vec2 sizeHint(vec2) { return vec2(0); }
    float pitch = PI/6, yaw = PI/2;
    shared<Graphics> graphics(vec2 unused size) {
        const int W=size.x, H=size.y;
        Image target = Image(int2(size));
        target.clear(0);
        float Mxx = cos(yaw), Mxy = -sin(yaw);
        float Myx = -sin(pitch)*sin(yaw), Myy = -sin(pitch)*cos(yaw), Myz = -cos(pitch);
        //byte4 colors[] = {byte4(0x50,0x60,0x80), byte4(0,0x40,0), byte4(0,0x80,0), byte4(0,0xFF,0), byte4(0x40,0x40,0xA0), 0xFF};
        for(Point p: points) {
            int x = p.position.x, y = p.position.y, z = p.position.z;
            int X = W/2 + Mxx * x + Mxy * y;
            int Y = H/2 + Myx * x + Myy * y + Myz * z;
            if(X >= 0 && Y>= 0 && Y < H && X < W) target(X, Y) = p.color;
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        yaw += PI/120;
        window->render();
        return graphics;
    }
} view;
