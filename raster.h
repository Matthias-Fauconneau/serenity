#pragma once
#include "image.h"

/// Framebuffer
extern Image framebuffer;

/// Clip
struct Clip { int2 min,max; Clip(int2 min, int2 max):min(min),max(max){}};
void push(Clip clip);
void pop();
void finish();

enum Blend { Opaque, Alpha, Multiply, MultiplyAlpha };
/// Fill framebuffer area between [target+min, target+max] with \a color
void fill(int2 target, int2 min, int2 max, byte4 color, Blend blend=Opaque);

/// Blit \a source to framebuffer at \a target
void blit(int2 target, const Image& source, Blend blend=Opaque, int alpha=255);

inline byte4 gray(int level) { return byte4(level,level,level,255); }
const byte4 white = gray(255);
const byte4 black = gray(0);
