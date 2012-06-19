#include "display.h"
#include "linux.h"
#include "array.cc"
Array_Copy(Rect)

#include "vector.cc"
vector(xy,int,2)

Pixmap framebuffer;
int2 screen;

struct VScreen { uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint reserved[32]; };
struct FScreen { char id[16]; ulong smem_start; uint p1[4]; uint16 p2[3]; uint stride; ulong mmio; uint p3[2]; uint16 p4[3]; };
enum { FBIOGET_VSCREENINFO=0x4600, FBIOPUT_VSCREENINFO, FBIOGET_FSCREENINFO };
void openDisplay() {
    int fd = open("/dev/fb0", O_RDWR, 0);
    FScreen fScreen; ioctl(fd, FBIOGET_FSCREENINFO, &fScreen);
    VScreen vScreen; ioctl(fd, FBIOGET_VSCREENINFO, &vScreen);
    int2 screen = int2(vScreen.xres,vScreen.yres);
    int size = screen.x * screen.y * 2;
    rgb* fb = (rgb*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    framebuffer = Image<rgb>(fb, screen.x, screen.y, fScreen.stride/2, false);
}

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
            rgb& d = framebuffer(x,y);
            int4 t = (int4)d;
            if(blend == Opaque) d=s;
            else if(blend==Multiply) d = byte4((int4(s)*t)/255);
            else if(blend==Alpha) d = byte4((s.a*int4(s) + (255-s.a)*t)/255);
        }
}

/// Blit

void blit(int2 target, const Pixmap& source, Blend blend, int alpha) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++)
        for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            rgb& d = framebuffer(x,y);
            int4 t = (int4)d;
            if(blend == Opaque) d=s;
            else if(blend==Multiply) d = byte4((int4(s)*t)/255);
            else if(blend==Alpha) d = byte4((s.a*int4(s) + (255-s.a)*t)/255);
            else if(blend==MultiplyAlpha) {
                int a = max(int(t.a),s.a*alpha/255);
                if(t.a) d = byte4((int4(s)*t*255/t.a)*a/255/255);//FIXME: composite, d.a=a;
            }
        }
}
