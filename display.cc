#include "display.h"
#include "gl.h"

bool softwareRendering;
array<Rect> clipStack;
Rect currentClip=Rect(0);
Image framebuffer;
SHADER(fill);
SHADER(blit);

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
        glBlend(color.w!=1, true);
        fillShader()["color"] = color;
        glDrawRectangle(fillShader(), rect);
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
        glBlend(source.alpha, true);
        blitShader()["color"] = color;
        blitShader().bindSamplers("sampler"); GLTexture::bindSamplers(source);
        glDrawRectangle(blitShader(), target+Rect(source.size()), true);
    }
}

void line(vec2 p1, vec2 p2, vec4 color) {
    if(!softwareRendering) {
        glBlend(true, false);
        fillShader()["color"] = vec4(vec3(1)-color.xyz(),1.f);
        glDrawLine(fillShader(), p1, p2);
    }
}
