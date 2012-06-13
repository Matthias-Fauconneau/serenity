#include "raster.h"

#include "array.cc"
Array_Copy(Rect)

#include "vector.cc"
vector(xy,int,2)
vector(xy,float,2)

Image framebuffer;

/// Clip

array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0),int2(0,0));
void push(Rect clip) {
    clipStack << currentClip;
    if(currentClip.max) currentClip=currentClip.clip(clip);
    else currentClip=clip;
}
void pop() { currentClip=clipStack.pop(); assert(clipStack); }
void finish() { clipStack.pop(); assert(!clipStack); currentClip=Rect(int2(0,0),int2(0,0)); }

/// Fill

void fill(Rect rect, byte4 color, Blend blend) {
    rect=rect.clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++)
        for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = color;
            byte4& d = framebuffer(x,y);
            if(blend == Opaque) d=s;
            else if(blend==Multiply) d = byte4((int4(s)*int4(d))/255);
            else if(blend==Alpha) d = byte4((s.a*int4(s) + (255-s.a)*int4(d))/255);
        }
}

/// Blit

void blit(int2 target, const Image& source, Blend blend, int alpha) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++)
        for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            byte4& d = framebuffer(x,y);
            if(blend == Opaque) d=s;
            else if(blend==Multiply) d = byte4((int4(s)*int4(d))/255);
            else if(blend==Alpha) d = byte4((s.a*int4(s) + (255-s.a)*int4(d))/255);
            else if(blend==MultiplyAlpha) {
                int a = max(int(d.a),s.a*alpha/255);
                if(d.a) d = byte4((int4(s)*int4(d)*255/d.a)*a/255/255), d.a=a;
            }
        }
}
