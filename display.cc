#include "display.h"
#if GL
#include "gl.h"
#endif

bool softwareRendering;
array<Rect> clipStack;
Rect currentClip=Rect(0);
Image framebuffer;
SRGB sRGB;
#if GL
SHADER(fill);
SHADER(blit);
#endif

void fill(Rect rect, vec4 color) {
    rect = rect & currentClip;
    if(softwareRendering) {
        int4 color8 = int4(color.z*255,color.y*255,color.x*255,color.w*255);
        if(color8.a == 0xFF) {
            for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = byte4(color8);
        } else {
            for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4& d = framebuffer(x,y); int a=color8.a;
                d = byte4((int4(d)*(0xFF-a) + color8*a)/0xFF);
            }
        }
    } else {
#if GL
        glBlend(color.w!=1, true);
        fillShader()["color"] = color;
        glDrawRectangle(fillShader(), rect);
#endif
    }
}

void blit(int2 target, const Image& source, vec4 color) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(softwareRendering) {
        int4 color8 = int4(color.z*255,color.y*255,color.x*255,color.w*255);
        if(source.alpha) {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                byte4 s = source(x-target.x,y-target.y); int a=color8.a*int(s.a)/0xFF;
                byte4& d = framebuffer(x,y);
                byte4 t = byte4((int4(d)*(0xFF-a) + color8*int4(s)/0xFF*a)/0xFF); t.a=min(0xFF,d.a+a);
                d = t;
            }
        } else {
            for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
                framebuffer(x,y) = source(x-target.x,y-target.y);
            }
        }
    } else {
#if GL
        glBlend(source.alpha, true);
        blitShader()["color"] = color;
        GLTexture texture = source; //FIXME
        blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(texture);
        glDrawRectangle(blitShader(), target+Rect(source.size()), true);
#endif
    }
}

inline void plot(int x, int y, float c, bool transpose, int4 invert) {
    if(transpose) swap(x,y);
    if(x>=currentClip.min.x && x<currentClip.max.x && y>=currentClip.min.y && y<currentClip.max.y) {
        byte4& d = framebuffer(x,y);
        d=byte4(max<int>(0,d.b-c*invert.b),max<int>(0,d.g-c*invert.g),max<int>(0,d.r-c*invert.r),min<int>(255,d.a+c*invert.a));
    }
}
inline float fpart(float x) { return x-int(x); }
inline float rfpart(float x) { return 1 - fpart(x); }

void line(vec2 p1, vec2 p2, vec4 color) {
    if(softwareRendering) {
        float x1=p1.x, y1=p1.y, x2=p2.x, y2=p2.y;
        int4 invert = int4(0xFF*(1-color.z),0xFF*(1-color.y),0xFF*(1-color.x),0xFF*color.w);
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
            intery = yend + gradient;
        }
        {
            float xend = round(x2), yend = y2 + gradient * (xend - x2);
            float xgap = fpart(x2 + 0.5);
            plot(int(xend), int(yend), rfpart(yend) * xgap, transpose, invert);
            plot(int(xend), int(yend) + 1, fpart(yend) * xgap, transpose, invert);
            i2 = int(xend);
        }
        for(int x=i1+1;x<i2;x++) {
            plot(x, int(intery), rfpart(intery), transpose, invert);
            plot(x, int(intery)+1, fpart(intery), transpose, invert);
            intery += gradient;
        }
    } else {
#if GL
        glBlend(true, false);
        fillShader()["color"] = vec4(vec3(1)-color.xyz(),1.f);
        glDrawLine(fillShader(), p1, p2);
#endif
    }
}

void disk(vec2 p, float r, vec4 color) {
    if(!softwareRendering) error("Unsupported");
    Rect rect = Rect(int2(p-vec2(r)),int2(ceil(p+vec2(r)))) & currentClip;
    byte4 color8 = byte4(color.z*255,color.y*255,color.x*255,color.w*255);
    vec4 negative = 255.f*vec4(1-color.z,1-color.y,1-color.x,color.w);
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        float d = r - sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y));
        if(d>1) framebuffer(x,y) = color8;
        else if(d>0) {
            byte4& t = framebuffer(x,y);
            t = byte4(max(0.f,t.b-d*negative.x),max(0.f,t.g-d*negative.y),max(0.f,t.r-d*negative.z),t.a);
        }
    }
}
