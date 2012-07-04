#include "display.h"
#include "linux.h"

/// int2

#include "vector.cc"
vector(xy,int,2)

/// Clip

#include "array.cc"
Array_Copy(Rect)

array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));
void push(Rect clip) {
    clipStack << currentClip;
    currentClip=currentClip.clip(clip);
}
void pop() { currentClip=clipStack.pop(); assert(clipStack); }
void finish() { clipStack.pop(); assert(!clipStack); currentClip=Rect(int2(0,0),int2(0,0)); }

/// Display

Pixmap framebuffer;
int2 screen;

struct VScreen { uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint reserved[32]; };
struct FScreen { char id[16]; ulong addr; uint len,type,p1[2]; uint16 p2[3]; uint stride; ulong mmio; uint p3[2]; uint16 p4[3]; };
enum { FBIOGET_VSCREENINFO=0x4600, FBIOPUT_VSCREENINFO, FBIOGET_FSCREENINFO };

void openDisplay() {
    int fd = open("/dev/fb0", O_RDWR, 0); assert(fd>0);
    FScreen fScreen; ioctl(fd, FBIOGET_FSCREENINFO, &fScreen);
    VScreen vScreen; ioctl(fd, FBIOGET_VSCREENINFO, &vScreen);
    screen = int2(vScreen.xres,vScreen.yres); assert(screen);
    int size = fScreen.stride * screen.y; assert(fScreen.stride>=screen.x*sizeof(rgb),fScreen.stride,fScreen.type,screen.x,sizeof(rgb),vScreen.bits_per_pixel,fScreen.id);
    rgb* fb = (rgb*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); assert(fb);
    framebuffer = Image<rgb>(fb, screen.x, screen.y, fScreen.stride/2, false);
    currentClip = Rect(screen);
}

/// Fill

void fill(Rect rect, rgb color) {
    rect=rect.clip(currentClip);
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = color;
    }
}

/// Blit

void blit(int2 target, const Image<gray>& source) {
    Rect rect = (target+Rect(source.size())).clip(currentClip);
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = source(x-target.x,y-target.y);
    }
}
