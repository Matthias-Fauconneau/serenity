#include "raster.h"
#include "array.cc"

Image framebuffer;

/// Clip

array<Clip> clipStack;
Clip currentClip=Clip(int2(0,0),int2(0,0));
void push(Clip clip) {
    clipStack << currentClip;
    if(currentClip.max) currentClip=Clip(max(currentClip.min,clip.min),min(currentClip.max,clip.max));
    else currentClip=clip;
}
void pop() { currentClip=clipStack.pop(); assert(clipStack); }
void finish() { clipStack.pop(); assert(!clipStack); currentClip=Clip(int2(0,0),int2(0,0)); }

/// Fill

void fill(int2 target, int2 min, int2 max, byte4 color, Blend blend) {
    for(int y= ::max(target.y+min.y,currentClip.min.y);y< ::min<int>(target.y+max.y,currentClip.max.y);y++)
        for(int x= ::max(target.x+min.x,currentClip.min.x);x< ::min<int>(target.x+max.x,currentClip.max.x);x++) {
            byte4 s = color;
            byte4& d = framebuffer(x,y);
            if(blend == Opaque) d=s;
            else if(blend==Multiply) d = byte4((int4(s)*int4(d))/255);
            else if(blend==Alpha) d = byte4((s.a*int4(s) + (255-s.a)*int4(d))/255);
        }
}

/// Blit

void blit(int2 target, const Image& source, Blend blend, int alpha) {
    for(int y=max(target.y,currentClip.min.y);y<min<int>(target.y+source.height,currentClip.max.y);y++) {
        for(int x=max(target.x,currentClip.min.x);x<min<int>(target.x+source.width,currentClip.max.x);x++) {
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
}
