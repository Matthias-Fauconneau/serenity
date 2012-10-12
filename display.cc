#include "display.h"

array<Rect> clipStack;
Rect currentClip=Rect(0);
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
    int4 invert = int4(0xFF-color.b,0xFF-color.g,0xFF-color.r,color.a);
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        int4 s = int4(source(x-target.x,y-target.y))*invert/0xFF;
        byte4& d = framebuffer(x,y);
        d=byte4(max(int4(0),int4(d)-s));
    }
}

/// Xiaolin Wu's line algorithm
inline void plot(uint x, uint y, float c, bool transpose, int4 invert) {
    if(transpose) swap(x,y);
    if(x<framebuffer.width && y<framebuffer.height) {
        byte4& d = framebuffer(x,y);
        d=byte4(max<int>(0,d.b-c*invert.b),max<int>(0,d.g-c*invert.g),max<int>(0,d.r-c*invert.r),min<int>(255,d.a+c*invert.a));
    }
}
inline float fpart(float x) { return x-int(x); }
inline float rfpart(float x) { return 1 - fpart(x); }
void line(float x1, float y1, float x2, float y2, byte4 color) {
    int4 invert = int4(0xFF-color.b,0xFF-color.g,0xFF-color.r,color.a);
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) swap(x1, y1), swap(x2, y2), swap(dx, dy), transpose=true;
    if(x2 < x1) swap(x1, x2), swap(y1, y2);
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(x1), yend = y1 + gradient * (xend - x1);
        float xgap = rfpart(x1 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, invert);
        plot(int(xend), int(yend)+1, fpart(yend) * xgap, transpose, invert);
        i1 = int(xend);
        intery = yend + gradient; // first y-intersection for the main loop
    }
    {
        float xend = round(x2), yend = y2 + gradient * (xend - x2);
        float xgap = fpart(x2 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, invert);
        plot(int(xend), int(yend) + 1, fpart(yend) * xgap, transpose, invert);
        i2 = int(xend);
    }

    // main loop
    for(int x=i1+1;x<i2;x++) {
        plot(x, int(intery), rfpart(intery), transpose, invert);
        plot(x, int(intery)+1, fpart(intery), transpose, invert);
        intery += gradient;
    }
}

// Thick line hack
void line(float x1, float y1, float x2, float y2, float w, byte4 color) {
    if(w<=1 || w>2) line(x1,y1,x2,y2,color);
    else if(w<=3) {
        float dx = x2 - x1, dy = y2 - y1;
        if(abs(dx)<abs(dy)) {
            line(x1-w/4,y1,x2-w/4,y2,color);
            line(x1+w/4,y1,x2+w/4,y2,color);
        } else {
            line(x1,y1-w/4,x2,y2-w/4,color);
            line(x1,y1+w/4,x2,y2+w/4,color);
        }
    }
}
