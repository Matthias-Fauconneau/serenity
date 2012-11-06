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

template<uint N> inline void polygon(vec2 polygon[N], byte4 color) {
    vec2 min=polygon[0],max=polygon[0];
    float lines[N][3]; // cross(P-A,B-A) > 0 <=> (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1) > 0 <=> δx*y + δy*x > c (with c=y1*δx-x1*δy)
    for(int i: range(N-1)) {
        min=::min(min,polygon[i+1]), max=::max(max,polygon[i+1]);
        lines[i][0] = polygon[i+1].x-polygon[i].x;
        lines[i][1] = polygon[i+1].y-polygon[i].y;
        lines[i][2] = polygon[i].y*lines[i][0] - polygon[i].x*lines[i][1];
        if(lines[i][1]>0 || (lines[i][1]==0 && lines[i][0]<0)) lines[i][2]++; //top-left fill rule
        float l = sqrt(lines[i][0]*lines[i][0]+lines[i][1]*lines[i][1]);
        lines[i][0]/=l, lines[i][1]/=l, lines[i][2]/=l; // normalize distance equation (for line smoothing)
    }
    lines[N-1][0] = polygon[0].x-polygon[N-1].x;
    lines[N-1][1] = polygon[0].y-polygon[N-1].y;
    lines[N-1][2] = polygon[N-1].y*lines[N-1][0] - polygon[N-1].x*lines[N-1][1];
    min = ::max(min,vec2(0,0)), max=::min(max,vec2(framebuffer.size()));
    for(float y=min.y; y<max.y; y++) for(float x=min.x; x<max.x; x++) {
        for(uint i=0; i<N; i++) {
            float d = lines[i][0]*y-lines[i][1]*x-lines[i][2];
            if(d>sqrt(2)/2) goto done; //outside
        }
        for(uint i=0; i<N; i++) { //smooth edges (TODO: MSAA, sRGB)
            float d = lines[i][0]*y-lines[i][1]*x-lines[i][2];
            if(d>-sqrt(2)/2) {
                d = d/sqrt(2)+0.5; assert(d>=0 && d<=1);
                framebuffer(x,y)=byte4((1-d)*vec4(color)+d*vec4(framebuffer(x,y)));
                goto done;
            }
        }
        framebuffer(x,y)=color; //completely inside
        done:;
    }
}
template void polygon<3>(vec2 polygon[3], byte4 color);
template void polygon<4>(vec2 polygon[4], byte4 color);

// Wide lines (oriented rectangle)
void line(vec2 A, vec2 B, float wa, float wb, byte4 color) {
    vec2 T = B-A;
    float l = length(T);
    if(l<0.01) return;
    vec2 N = normal(T)/l;
    quad(A+N*(wa/2),B+N*(wb/2),B-N*(wb/2),A-N*(wa/2),color);
}
