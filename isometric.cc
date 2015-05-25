#include "thread.h"
#include "window.h"
#include "gl.h"

struct Isometric : Widget {
    unique<Window> window = ::window(this, 0);
    vec2 sizeHint(vec2) { return vec2(1366, 720); }
    shared<Graphics> graphics(vec2 unused size) {
        Image target = Image(int2(size));
        target.clear(0);
        const int n = 16, N = 32;
        /*for(int y: range(size.y/S)) {
            for(int x: range(size.x/S)) {
                target(x*S,y*S) = 0;
            }
        }*/
        for(int y: range(-N,N)) {
            for(int x: range(-N,N)) {
                int Z = int(sin(2*PI*float(x)/N)*sin(2*PI*float(y)/N)*4);
                for(int z: range(Z-1, Z+1)) {
                    int X = x + y;
                    int Y = x - y - z*2;
                    int ix = (size.x/n)/2 + X/2, fx = X%2;
                    int iy = (size.y/n)/2 + Y/4, fy = Y%4;
                    uint px = ix*n+fx*n/2;
                    uint py = iy*n+fy*n/4;
                    if(px < uint(target.size.x) && py < uint(target.size.y)) target(px, py) = byte4((4+z)*0xFF/8, fx*0xFF/2, fy*0xFF/4, 0xFF);
                }
            }
        }
        shared<Graphics> graphics;
        graphics->blits.append(0, size, move(target));
        return graphics;
    }
} view;
