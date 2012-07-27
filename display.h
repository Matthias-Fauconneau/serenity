#pragma once
#include "vector.h"
#include "image.h"

#define RGB565 1
#if RGB565
struct rgb565 {
    uint16 pack;
    rgb565():pack(0){}
    rgb565(uint8 i):pack( (i&0b11111000)<<8 | (i&0b11111100)<<3 | i>>3 ) {}
    rgb565(uint8 b, uint8 g, uint8 r):pack( (r&0b11111000)<<8 | (g&0b11111100)<<3 | b>>3 ) {}
    rgb565(byte4 c):rgb565(c.b, c.g, c.r){}
    operator byte4() { return byte4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
    operator   int4() { return     int4((pack&0b11111)<<3,(pack>>3)&0b11111100,pack>>8,255); }
};
typedef rgb565 rgb;
#else
typedef byte4 rgb;
#endif

/// Clip
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min{0,0},max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    /// Intersect this clip with \a clip
    Rect clip(Rect clip) { return Rect(::max(min,clip.min),::min(max,clip.max)); }
    bool contains(int2 p) { return p>=min && p<max; }
    explicit operator bool() { return (max-min)>int2(0,0); }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline string str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }

void push(Rect clip);
void pop();
void finish();

/// Returns display size (and on first call mmap framebuffer)
int2 display();

/// Fill framebuffer area in \a rect with \a color
void fill(Rect rect, rgb color);

/// Blit \a source to framebuffer at \a target
void blit(int2 target, const Image<uint8>& source); //TODO: color
void blit(int2 target, const Image<rgb565>& source);
void blit(int2 target, const Image<byte4>& source); //TODO: alpha

/// Fast software cursor
void patchCursor(int2 position, const Image<byte4>& cursor, bool repair=true);
