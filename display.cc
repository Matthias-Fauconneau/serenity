#include "display.h"
#include "linux.h"
#include "array.cc"
#include "vector.cc"
vector(xy,int,2)

/// Clip

array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));
void push(Rect clip) {
    clipStack << currentClip;
    currentClip=currentClip.clip(clip);
}
void pop() { assert(clipStack); currentClip=clipStack.pop(); }

/// Display

Image<rgb> framebuffer;

struct VScreen { uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint reserved[32]; };
struct FScreen { char id[16]; void* addr; uint len,type,p1[2]; uint16 p2[3]; uint stride; void* mmio; uint p3[2]; uint16 p4[3]; };
enum { FBIOGET_VSCREENINFO=0x4600, FBIOPUT_VSCREENINFO, FBIOGET_FSCREENINFO };

int2 display() {
    static int2 display;
    if(!display) {
        int fd = open("/dev/fb0", O_RDWR, 0); assert(fd>0);
        FScreen fScreen; ioctl(fd, FBIOGET_FSCREENINFO, &fScreen);
        VScreen vScreen; ioctl(fd, FBIOGET_VSCREENINFO, &vScreen);
        display = int2(vScreen.xres,vScreen.yres); assert(display);
        int size = fScreen.stride * display.y; assert(fScreen.stride>=display.x*sizeof(rgb),fScreen.stride,fScreen.type,display.x,sizeof(rgb),vScreen.bits_per_pixel,fScreen.id);
        rgb* fb = (rgb*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); assert(fb);
        framebuffer = Image<rgb>(fb, display.x, display.y, fScreen.stride/2, false, false);
        currentClip = Rect(display);
    }
    return display;
}

/// Fill

void fill(Rect rect, rgb color) {
    rect=rect.clip(currentClip);
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = color;
    }
}

/// Blit

void blit(int2 target, const Image<uint8>& source) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = source(x-target.x,y-target.y); //TODO: color
    }
}
void blit(int2 target, const Image<rgb565>& source) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = source(x-target.x,y-target.y);
    }
}
void blit(int2 target, const Image<byte4>& source) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            auto& d = framebuffer(x,y);
            d = byte4((int4(d)*(255-s.a) + int4(s)*s.a)/255); //TODO: lookup /255
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            framebuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}

void patchCursor(int2 position, const Image<byte4>& cursor, bool repair) {
    static Image<rgb565> patch(cursor.width,cursor.height);
    static int2 lastPosition;
    Rect oldRect = (lastPosition+Rect(patch.size())).clip(Rect(display()));
    Rect newRect = (position+Rect(patch.size())).clip(Rect(display()));
    if(oldRect&newRect) { //Move cursor in a single pass to avoid flicker
        Rect rect= oldRect|newRect;
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            rgb p = framebuffer(x,y);
            {uint dx=x-lastPosition.x, dy=y-lastPosition.y;
            if(repair && dx<patch.width && dy<patch.height) p=patch(dx,dy);}
            {uint dx=x-position.x, dy=y-position.y;
            if(dx<patch.width&&dy<patch.width) {
                patch(dx,dy) = p;
                byte4 s = cursor(dx,dy);
                p = byte4((int4(p)*(256-s.a) + int4(s)*s.a)/256);
            }}
            framebuffer(x,y) = p;
        }
    } else {
        blit(lastPosition,patch);
        Rect rect = (position+Rect(patch.size())).clip(Rect(display()));
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            patch(x-position.x,y-position.y) = framebuffer(x,y);
        }
        blit(position,cursor);
        lastPosition = position;
    }
    lastPosition = position;
}
