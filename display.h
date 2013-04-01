#pragma once
/// \file display.h 2D graphics primitives (fill, blit, line)
#include "vector.h"
#include "image.h"

#define GL 0
#if GL
extern bool softwareRendering;
#endif

/// Framebuffer of the window being rendered (X11 shared memory to be used only under Window::render)
extern Image framebuffer;

// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

// Colors
constexpr vec4 black(0, 0, 0, 1);
constexpr vec4 darkGray(13./16, 13./16, 13./16, 1);
constexpr vec4 lightGray (15./16, 15./16, 15./16, 1);
constexpr vec4 white (1, 1, 1, 1);
constexpr vec4 highlight (8./16, 12./16, 14./16, 1);
constexpr vec4 blue (0, 0, 1, 1);
constexpr vec4 cyan (0, 1, 1, 1);
constexpr vec4 green (0, 1, 0, 1);
constexpr vec4 yellow (0, 1, 1, 1);
constexpr vec4 red (1, 0, 0, 1);
constexpr vec4 magenta (1, 0, 1, 1);

// Primitives
/// Fills pixels inside \a rect with \a color
void fill(Rect rect, vec4 color=black);
inline void fill(int x1, int y1, int x2, int y2, vec4 color=black) { fill(Rect(int2(x1,y1),int2(x2,y2)),color); }

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(int2 target, const Image& source, vec4 color=white);

/// Resizes \a source to \a size and blits at \a target
void blit(int2 target, const Image& source, int2 size);

/// Draws a thin antialiased line from p1 to p2
void line(vec2 p1, vec2 p2, vec4 color=black);
inline void line(int2 p1, int2 p2, vec4 color=black) { line(vec2(p1),vec2(p2),color); }
inline void line(int x1, int y1, int x2, int y2, vec4 color=black) { line(vec2(x1,y1),vec2(x2,y2),color); }

// Gamma correction
struct SRGB {
    uint8 lookup[256];
    inline float evaluate(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4f)-0.055; else return 12.92*c; }
    SRGB() { for(uint i=0;i<256;i++) { uint l = round(255*evaluate(i/255.f)); assert(l<256); lookup[i]=l; } }
    inline uint8 operator [](uint c) { assert(c<256,c); return lookup[c]; }
};
extern SRGB sRGB;
