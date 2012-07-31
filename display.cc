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
    currentClip=currentClip & clip;
}
void pop() { assert(clipStack); currentClip=clipStack.pop(); }

/// Display

Image<pixel> backbuffer;
Image<pixel> frontbuffer;

struct VScreen { uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint reserved[32]; };
struct FScreen { char id[16]; void* addr; uint len,type,p1[2]; uint16 p2[3]; uint stride; void* mmio; uint p3[2]; uint16 p4[3]; };
enum { FBIOGET_VSCREENINFO=0x4600, FBIOPUT_VSCREENINFO, FBIOGET_FSCREENINFO, FBIOGETCMAP, FBIOPUTCMAP, FBIOPAN_DISPLAY,
       FBIO_WAITFORVSYNC=('O'<<8)+57};

static int fd;
static FScreen fScreen;
static VScreen vScreen;
int2 display() {
    static int2 display;
    if(!display) {
        fd = open("/dev/fb0", O_RDWR, 0); assert(fd>0);
        ioctl(fd, FBIOGET_FSCREENINFO, &fScreen);
        ioctl(fd, FBIOGET_VSCREENINFO, &vScreen);
        //vScreen.yres_virtual *= 2;
        //vScreen.yoffset=vScreen.yres;
        //ioctl(fd, FBIOPUT_VSCREENINFO, &vScreen);
        display = int2(vScreen.xres,vScreen.yres); assert(display);
        pixel* fb = (pixel*)mmap(0, fScreen.stride * display.y, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0); assert(fb);
        //backbuffer = Image<pixel>(display.x, display.y, fScreen.stride/2);
        frontbuffer = Image<pixel>(fb, display.x, display.y, fScreen.stride/2, false, false);
        backbuffer = share(frontbuffer);
        currentClip = Rect(display);
        //vScreen.yoffset=vScreen.yres;
        //ioctl(fd, FBIOPAN_DISPLAY, &vScreen);
    }
    return display;
}

void swapBuffers() {
    /*if(vScreen.yoffset) { vScreen.yoffset=0; framebuffer.data+=vScreen.yres*fScreen.stride/2;
    } else { vScreen.yoffset=vScreen.yres; framebuffer.data-=vScreen.yres*fScreen.stride/2; }
    //ioctl(fd, FBIOPUT_VSCREENINFO, &vScreen);
    ioctl(fd, FBIOPAN_DISPLAY, &vScreen);*/
    //int arg=0; ioctl(fd, FBIO_WAITFORVSYNC, &arg);
    //::copy(frontbuffer.data,backbuffer.data,backbuffer.stride*backbuffer.height);
}

/// Fill

void fill(Rect rect, pixel color) {
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        backbuffer(x,y) = color;
    }
}

/// Blit

void blit(int2 target, const Image<uint8>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        backbuffer(x,y) = source(x-target.x,y-target.y); //TODO: color
    }
}
void blit(int2 target, const Image<pixel>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        backbuffer(x,y) = source(x-target.x,y-target.y);
    }
}
void blit(int2 target, const Image<byte4>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    if(source.alpha) {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = source(x-target.x,y-target.y);
            auto& d = backbuffer(x,y);
            d = byte4((int4(d)*(255-s.a) + int4(s)*s.a)/255); //TODO: lookup /255
        }
    } else {
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            backbuffer(x,y) = source(x-target.x,y-target.y);
        }
    }
}

void patchCursor(int2 position, const Image<byte4>& cursor, bool repair) {
    static Image<pixel> patch(cursor.width,cursor.height);
    static int2 lastPosition;
    Rect oldRect = (lastPosition+Rect(patch.size())) & Rect(display());
    Rect newRect = (position+Rect(patch.size())) & Rect(display());
    if(oldRect&newRect) { //Move cursor in a single pass to avoid flicker
        Rect rect= oldRect|newRect;
        static Image<pixel> newPatch(cursor.width,cursor.height);
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            pixel p = frontbuffer(x,y);
            {uint dx=x-lastPosition.x, dy=y-lastPosition.y;
            if(repair && dx<patch.width && dy<patch.height) p=patch(dx,dy);}
            {uint dx=x-position.x, dy=y-position.y;
            if(dx<patch.width&&dy<patch.width) {
                newPatch(dx,dy) = p;
                byte4 s = cursor(dx,dy);
                p = byte4((int4(p)*(256-s.a) + int4(s)*s.a)/256);
            }}
            frontbuffer(x,y) = p;
        }
        swap(patch,newPatch);
    } else {
        {Rect rect = (lastPosition+Rect(patch.size())) & Rect(display());
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = patch(x-lastPosition.x,y-lastPosition.y);
            auto& d = frontbuffer(x,y);
            d = byte4((int4(d)*(255-s.a) + int4(s)*s.a)/255); //TODO: lookup /255
        }}
        {Rect rect = (position+Rect(patch.size())) & Rect(display());
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = cursor(x-position.x,y-position.y);
            auto& d = frontbuffer(x,y);
            patch(x-position.x,y-position.y) = d;
            d = byte4((int4(d)*(255-s.a) + int4(s)*s.a)/255); //TODO: lookup /255
        }}
        lastPosition = position;
    }
    lastPosition = position;
}
