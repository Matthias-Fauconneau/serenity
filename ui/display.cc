#include "display.h"
#include "math.h"

bool softwareRendering;
int resolution = 96;
Image framebuffer;
Lock framebufferLock;
array<Rect> clipStack;
Rect currentClip=Rect(0);

typedef vector<bgra,int,4> int4;

void fill(Rect rect, vec4 color) {
    rect = rect & currentClip;
    int4 color8 (color.z*0xFF,color.y*0xFF,color.x*0xFF,color.w*0xFF);
    if(color8.a == 0xFF) {
        for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = byte4(color8);
    } else {
        error("");
        /*for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4& d = framebuffer(x,y); int a=color8.a;
            d = byte4((int4(d)*(0xFF-a) + color8*a)/0xFF);
        }*/
    }
}

void blit(int2 target, const Image& source, vec4 unused color) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        int4 color8 = int4(color.z*0xFF,color.y*0xFF,color.x*0xFF,color.w*0xFF);
        if(color==white) {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=s.a;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + int4(s)*a)/0xFF); t.a=0xFF;
                d = t;
            }
        } else {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=color8.a*int(s.a)/0xFF;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + color8*int4(s)/0xFF*a)/0xFF); t.a=0xFF;//min(0xFF,d.a+a);
                d = t;
            }
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 t = source(x-target.x,y-target.y); t.a = 0xFF;
            framebuffer(x,y) = t;
        }
    }
}

void bilinear(Image& target, const Image& source);
void blit(int2 target, const Image& source, int2 size) {
    Image region = clip(framebuffer, target, size);
    bilinear(region, source);
}

inline void plot(int x, int y, float alpha, bool transpose, int4 color) {
    if(transpose) swap(x,y);
    if(x>=currentClip.min.x && x<currentClip.max.x && y>=currentClip.min.y && y<currentClip.max.y) {
        byte4& d = framebuffer(x,y);
        d=byte4(round((1-alpha)*d.b+alpha*color.b),round((1-alpha)*d.g+alpha*color.g),round((1-alpha)*d.r+alpha*color.r),0xFF);
    }
}

inline float fpart(float x) { return x-int(x); }
inline float rfpart(float x) { return 1 - fpart(x); }
void line(vec2 p1, vec2 p2, vec4 color) {
    float x1=p1.x, y1=p1.y, x2=p2.x, y2=p2.y;
    assert(vec4(0) <= color && color <= vec4(1));
    int4 color8 = int4(0xFF*color.z,0xFF*color.y,0xFF*color.x,0xFF*color.w);
    float dx = x2 - x1, dy = y2 - y1;
    bool transpose=false;
    if(abs(dx) < abs(dy)) swap(x1, y1), swap(x2, y2), swap(dx, dy), transpose=true;
    if(x2 < x1) swap(x1, x2), swap(y1, y2);
    float gradient = dy / dx;
    int i1,i2; float intery;
    {
        float xend = round(x1), yend = y1 + gradient * (xend - x1);
        float xgap = rfpart(x1 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, color8);
        plot(int(xend), int(yend)+1, fpart(yend) * xgap, transpose, color8);
        i1 = int(xend);
        intery = yend + gradient;
    }
    {
        float xend = round(x2), yend = y2 + gradient * (xend - x2);
        float xgap = fpart(x2 + 0.5);
        plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, color8);
        plot(int(xend), int(yend) + 1, fpart(yend) * xgap, transpose, color8);
        i2 = int(xend);
    }
    for(int x=i1+1;x<i2;x++) {
        plot(x, int(intery), rfpart(intery), transpose, color8);
        plot(x, int(intery)+1, fpart(intery), transpose, color8);
        intery += gradient;
    }
}

vec3 HSVtoRGB(float h, float s, float v) {
    float H = h/PI*3, C = v*s, X = C*(1-abs(mod(H,2)-1));
    int i=H;
    if(i==0) return vec3(C,X,0);
    if(i==1) return vec3(X,C,0);
    if(i==2) return vec3(0,C,X);
    if(i==3) return vec3(0,X,C);
    if(i==4) return vec3(X,0,C);
    if(i==5) return vec3(C,0,X);
    return vec3(0,0,0);
}

