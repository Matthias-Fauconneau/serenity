#include "thread.h"
#include "window.h"
#include "gl.h"
#define big16 __builtin_bswap16

struct Isometric : Widget {
    unique<Window> window = ::window(this, 0);
    vec2 sizeHint(vec2) { return vec2(1366, 720); }
    shared<Graphics> graphics(vec2 unused size) {
        Map map ("N47E008.hgt"_, home()); // (30, 30, 1)
        Image16 elevation (3601, 3601);
        uint min = -1, max = 0;
        for(int y: range(elevation.size.y)) for(int x: range(elevation.size.x)) {
            uint s = big16(cast<uint16>(map)[y*3601+x]);
            elevation(x, y) = s;
            min = ::min(min, s);
            max = ::max(max, s);
        }
        // Evaluate
        struct Voxel { float intensity; vec3 normal; };
        struct Column { int z0; int count; Voxel voxels[3]; };
        const int N = 2048; // 64 B/column, 2K, 256MB
        ImageT<Column> columns (N, N);
        for(int y: range(N)) {
            for(int x: range(N)) {
                auto& column = columns(x, y);
                int z = elevation(1+x, 1+y);
                column.z0 = z/30 - 1;
                column.count = 3;
                float dx = 2*30, dy = 2*30;
                float dxz = elevation(1+x+1, 1+y) - elevation(1+x-1, 1+y);
                float dyz = elevation(1+x, 1+y+1) - elevation(1+x, 1+y+1);
                vec3 normal = normalize(vec3(-dy*dxz, -dx*dyz, dx*dy));
                float a = float(z-min)/float(max-min);
                float l = dot(normal, normalize(vec3(-1,-1,1)));
                for(int dz: range(column.count)) column.voxels[dz] = {a*l, normal};
            }
        }
        // Sample
        const int W=size.x, H=size.y;
        buffer<float3> samples (H*W*4);
        for(int y: range(N)) {
            for(int x: range(N)) {
                const auto& column = columns(x, y);
                for(int dz: range(column.count)) {
                    int z = column.z0 + dz;
                    int X = W + (x - N/2) + (y - N/2);
                    int Y = H*2 - (x - N/2) + (y - N/2) - z;
                    int ix = X/2, fx = X%2;
                    int iy = Y/4, fy = Y%4;
                    if(iy>=0 && iy < H && ix >= 0 && ix < W) samples[(iy*W+ix)*4+fy/2*2+fx] = float3(column.voxels[dz].intensity);
                }
            }
        }
        // Resolve
        Image target = Image(int2(size));
        for(size_t i: range(H*W)) {
            float3 sum = 0;
            for(float3 sample: samples.slice(i*4, 4)) sum += sample;
            sum /= 4;
            int3 linear (round(float(0xFFF)*sum));
            extern const uint8 sRGB_forward[0x1000];
            target[i] = byte4(sRGB_forward[linear[0]], sRGB_forward[linear[1]], sRGB_forward[linear[2]], 0xFF);
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        return graphics;
    }
} view;
