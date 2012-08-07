#include "display.h"
#include "linux.h"
#include "array.cc"

/// Clip

void remove(array<Rect>& rects, Rect neg) {
    for(uint j=0;j<rects.size();) { Rect rect=rects[j];
        if(rect&neg) { //split
            rects.removeAt(j);
            {Rect r(rect.min,int2(rect.max.x,neg.min.y)); if(r) rects<<r;} //top
            {Rect r(int2(rect.min.x,max(rect.min.y,neg.min.y)), int2(neg.min.x,min(rect.max.y,neg.max.y))); if(r) rects<<r;} //left
            {Rect r(int2(neg.max.x,max(rect.min.y,neg.min.y)), int2(rect.max.x,min(rect.max.y,neg.max.y))); if(r) rects<<r;} //right
            {Rect r(int2(rect.min.x,neg.max.y),rect.max); if(r) rects<<r;} //bottom
            //TODO: merge when possible
        } else j++;
    }
}

array<Rect> clipStack;
Rect currentClip=Rect(int2(0,0));
void push(Rect clip) {
    clipStack << currentClip;
    currentClip=currentClip & clip;
}
void pop() { assert(clipStack); currentClip=clipStack.pop(); }

/// Display
Image<pixel> framebuffer;

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
        framebuffer = Image<pixel>(fb, display.x, display.y, fScreen.stride/2, false, false);
        currentClip = Rect(display);
        //vScreen.yoffset=vScreen.yres;
        //ioctl(fd, FBIOPAN_DISPLAY, &vScreen);
    }
    return display;
}

/// Fill

void fill(Rect rect, pixel color) {
    rect = rect & currentClip;
    for(int y=rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) framebuffer(x,y) = color;
}

/// Blit

void blit(int2 target, const Image<uint8>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = source(x-target.x,y-target.y); //TODO: color
    }
}
void blit(int2 target, const Image<pixel>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
    for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
        framebuffer(x,y) = source(x-target.x,y-target.y);
    }
}
inline uint div255(uint x) { return (x+(x<<8)+257)>>16; } //exact for 16bit x
inline int4 div255(const int4& v) { int4 r; for(int i=0;i<4;i++) r[i]=div255(v[i]); return r; }
void blit(int2 target, const Image<byte4>& source) {
    Rect rect = (target+Rect(source.size())) & currentClip;
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
    static Image<pixel> patch(cursor.width,cursor.height);
    static int2 lastPosition;
    Rect oldRect = (lastPosition+Rect(patch.size())) & Rect(display());
    Rect newRect = (position+Rect(patch.size())) & Rect(display());
    if(oldRect&newRect) { //Move cursor in a single pass to avoid flicker
        Rect rect= oldRect|newRect;
        static Image<pixel> newPatch(cursor.width,cursor.height);
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            pixel p = framebuffer(x,y);
            {uint dx=x-lastPosition.x, dy=y-lastPosition.y;
            if(dx<patch.width && dy<patch.height) p=patch(dx,dy);}
            {uint dx=x-position.x, dy=y-position.y;
            if(dx<patch.width&&dy<patch.width) {
                newPatch(dx,dy) = p;
                byte4 s = cursor(dx,dy);
                p = byte4(div255(int4(p)*(255-s.a) + int4(s)*s.a));
            }}
            if(repair) framebuffer(x,y) = p;
        }
        swap(patch,newPatch);
    } else {
        {Rect rect = (lastPosition+Rect(patch.size())) & Rect(display());
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = patch(x-lastPosition.x,y-lastPosition.y);
            auto& d = framebuffer(x,y);
            if(repair) d = byte4(div255(int4(d)*(255-s.a) + int4(s)*s.a));
        }}
        {Rect rect = (position+Rect(patch.size())) & Rect(display());
        for(int y= rect.min.y; y<rect.max.y; y++) for(int x= rect.min.x; x<rect.max.x; x++) {
            byte4 s = cursor(x-position.x,y-position.y);
            auto& d = framebuffer(x,y);
            patch(x-position.x,y-position.y) = d;
            d = byte4(div255(int4(d)*(255-s.a) + int4(s)*s.a));
        }}
        lastPosition = position;
    }
    lastPosition = position;
}
