#include "thread.h"
#include "window.h"
#include "gl.h"

struct Isometric : Widget {
    unique<Window> window = ::window(this, 0);
    vec2 sizeHint(vec2) { return vec2(1366, 720); }
    shared<Graphics> graphics(vec2 unused size) {
        const int N = 512;
        const int W=size.x, H=size.y;
        buffer<float3> samples (H*W*4); // 4 samples / pixel
        for(int y: range(-N,N)) {
            for(int x: range(-N,N)) {
                const float A = 256;
                int Z = int(sin(PI*float(x)/N)*sin(PI*float(y)/N)*A);
                for(int z: range(Z-2, Z+1)) {
                    int X = W + x + y;
                    int Y = H*2 - x + y - z*2;
                    assert_(X>=0 && Y>=0, x, y, z, X, Y);
                    int ix = X/2, fx = X%2;
                    int iy = Y/4, fy = Y%4;
                    samples[(iy*W+ix)*4+fy/2*2+fx] = float3(max(0.f, (A+z)/(A*2)));
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
