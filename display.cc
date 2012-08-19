#include "display.h"

/// Clip
array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));

/// Render
Image<pixel> framebuffer;

void fill(Rect rect, pixel color) { //TODO: blend
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = color;
}

#if __arm__
inline uint div255(uint x) { return (x+(x<<8)+257)>>16; }
#else
inline uint div255(uint x) { return x/255; }
#endif
inline int4 div255(const int4& v) { int4 r; for(int i=0;i<4;i++) r[i]=div255(v[i]); return r; }

void blit(int2 target, const Image<uint8>& source, uint8 opacity, bool unused invert) { //TODO: invert
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(opacity==255) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            int s = source(x-target.x,y-target.y), a=255-s;
            pixel& d = framebuffer(x,y);
            byte4 t = byte4(div255(int4(d)*s)); t.a=min(255,d.a+a);
            d = t;
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            int s = source(x-target.x,y-target.y), a=255-s;
            pixel& d = framebuffer(x,y);
            byte4 t = byte4(div255(int4(d)*(255-a) + int4(s)*a)); t.a=min(255,d.a+a);
            d = t;
        }
    }
}

void blit(int2 target, const Image<byte4>& source, bool unused invert) { //TODO: invert
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            pixel& d = framebuffer(x,y);
            byte4 t = byte4(div255(int4(d)*(255-s.a) + int4(s)*s.a)); t.a=min(255,d.a+s.a);
            d = t;
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}
