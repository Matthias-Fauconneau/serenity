#pragma once
#include "vector.h"
#include "image.h"

#define RGB565 1
#if RGB565
struct rgb565 {
    uint16 pack;
    rgb565():pack(0){}
    rgb565(gray g):pack( (g.level&0b11111000)<<8 | (g.level&0b11111100)<<3 | g.level>>3 ) {}
    rgb565(uint8 r, uint8 g, uint8 b):pack( (r&0b11111000)<<8 | (g&0b11111100)<<3 | b>>3 ) {}
    //rgb565(byte4 c):rgb565(c.r, c.g, c.b){}
    //operator byte4() const { return byte4( (pack>>8)&0b11111000, (pack>>3)&0b11111100, pack<<3, 255); }
    //operator int4() const { return int4( (pack>>8)&0b11111000, (pack>>3)&0b11111100, (pack<<3)&0b11111000, 255); }
};
typedef rgb565 rgb;
#else
typedef byte4 rgb;
#endif
typedef Image<rgb> Pixmap; //Image in display format

/// Clip
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min(0,0),max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    /// Intersect this clip with \a clip
    Rect clip(Rect clip) { return Rect(::max(min,clip.min),::min(max,clip.max)); }
    /// Tests if this clip is empty
    explicit operator bool() { return (max-min)>int2(0,0); }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }

void push(Rect clip);
void pop();
void finish();

/// Display
void openDisplay();
extern int2 screen;

/// Fill framebuffer area in \a rect with \a color
void fill(Rect rect, rgb color);

/// Blit \a source to framebuffer at \a target
void blit(int2 target, const Image<gray>& source);
//void blit(int2 target, const Pixmap& source);
