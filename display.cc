#include "display.h"

#if __arm__
inline uint div255(uint x) { return (x+(x<<8)+257)>>16; }
#else
inline uint div255(uint x) { return x/255; }
#endif
inline int4 div255(const int4& v) { int4 r; for(int i=0;i<4;i++) r[i]=div255(v[i]); return r; }

/// Clip
array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));

/// Render
Image framebuffer;

void fill(Rect rect, byte4 color) { //TODO: blend
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        byte4& d = framebuffer(x,y);
        d = byte4(div255(int4(d)*(255-color.a) + int4(color)*color.a));
    }
}

void blit(int2 target, const Image& source, uint8 opacity) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y); int a=s.a*opacity/255;
            byte4& d = framebuffer(x,y);
            byte4 t = byte4(div255(int4(d)*(255-a) + int4(s)*a)); t.a=min(255,d.a+a);
            d = t;
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}

void multiply(int2 target, const Image& source, uint8 opacity) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        byte4 s = source(x-target.x,y-target.y); int a=s.a*opacity/255;
        byte4& d = framebuffer(x,y);
        byte4 t = byte4(div255(int4(d)*int4(s))); t.a=min(255,d.a+a);
        d = t;
    }
}
