#include "display.h"

/// Clip
array<Rect> clipStack;
Rect currentClip=Rect(0);

/// Render
Image framebuffer;

void fill(Rect rect, byte4 color, bool blend) {
    rect = rect & currentClip;
    if(!blend) for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = color;
    else for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        byte4& d = framebuffer(x,y); int a=color.a;
        d = byte4((int4(d)*(0xFF-a) + int4(color)*a)/0xFF);
    }
}

void blit(int2 target, const Image& source, uint8 opacity) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y); int a=s.a*opacity/0xFF;
            byte4& d = framebuffer(x,y);
            byte4 t = byte4((int4(d)*(0xFF-a) + int4(s)*a)/0xFF); t.a=min(0xFF,d.a+a);
            d = t;
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}

void substract(int2 target, const Image& source, byte4 color) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        int4 s = int4(source(x-target.x,y-target.y))*(int4(0xFF)-int4(color))/0xFF;
        byte4& d = framebuffer(x,y);
        d=byte4(max(int4(0),int4(d)-s));
    }
}
