#include "display.h"
#include "linux.h"

/// Clip

void remove(array<Rect>& rects, Rect neg) {
    for(uint j=0;j<rects.size();) { Rect rect=rects[j];
        if(rect&neg) { //split
            rects.removeAt(j);
            {Rect r(rect.min,int2(rect.max.x,neg.min.y)); if(r) rects<<r;} //top
            {Rect r(int2(rect.min.x,max(rect.min.y,neg.min.y)), int2(neg.min.x,min(rect.max.y,neg.max.y))); if(r) rects<<r;} //left
            {Rect r(int2(neg.max.x,max(rect.min.y,neg.min.y)), int2(rect.max.x,min(rect.max.y,neg.max.y))); if(r) rects<<r;} //right
            {Rect r(int2(rect.min.x,neg.max.y),rect.max); if(r) rects<<r;} //bottom
        } else j++;
    }
}

array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));

/// Primitives

Image<pixel> framebuffer;

void fill(Rect rect, pixel color) { //TODO: blend
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = color;
}
void blit(int2 target, const Image<uint8>& source, bool unused invert) { //TODO: blend, invert
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        uint8 value = source(x-target.x,y-target.y);
        framebuffer(x,y) = pixel(value,value,value,255);
    }
}
#if __arm__
inline uint div255(uint x) { return (x+(x<<8)+257)>>16; }
#else
inline uint div255(uint x) { return x/255; }
#endif
inline int4 div255(const int4& v) { int4 r; for(int i=0;i<4;i++) r[i]=div255(v[i]); return r; }
void blit(int2 target, const Image<byte4>& source, bool unused invert) { //TODO: invert
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            auto& d = framebuffer(x,y);
            d = byte4(div255(int4(d)*(255-s.a) + int4(s)*s.a));
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}
