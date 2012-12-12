#pragma once
/// \file display.h 2D graphics primitives (fill, blit, line)
#include "vector.h"
#include "image.h"
#include "process.h"

extern bool softwareRendering;
extern Lock framebufferLock;
/// Current window framebuffer (X11 shared memory mapped by Window::render)
extern Image framebuffer;

// Clip
extern array<Rect> clipStack;
extern Rect currentClip;
inline void push(Rect clip) { clipStack << currentClip; currentClip=currentClip & clip; }
inline void pop() { currentClip=clipStack.pop(); }

// Colors
constexpr vec4 black __(0, 0, 0, 1);
constexpr vec4 darkGray __(13./16, 13./16, 13./16, 1);
constexpr vec4 lightGray __(15./16, 15./16, 15./16, 1);
constexpr vec4 white __(1, 1, 1, 1);
constexpr vec4 darken __(0, 0, 0, 0);
constexpr vec4 lighten __(1, 1, 1, 1./4);
constexpr vec4 highlight __(8./16, 12./16, 14./16, 1);
constexpr vec4 blue __(0, 0, 1, 1);
constexpr vec4 cyan __(0, 1, 1, 1);
constexpr vec4 green __(0, 1, 0, 1);
constexpr vec4 yellow __(0, 1, 1, 1);
constexpr vec4 red __(1, 0, 0, 1);
constexpr vec4 magenta __(1, 0, 1, 1);

// Primitives
/// Fills pixels inside \a rect with \a color
void fill(Rect rect, vec4 color=black);
inline void fill(int x1, int y1, int x2, int y2, vec4 color=black) { fill(Rect(int2(x1,y1),int2(x2,y2)),color); }

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
void blit(int2 target, const Image& source, vec4 color=white);

/// Draws a thin antialiased line from p1 to p2
void line(vec2 p1, vec2 p2, vec4 color=black);
inline void line(int2 p1, int2 p2, vec4 color=black) { line(vec2(p1),vec2(p2),color); }
inline void line(int x1, int y1, int x2, int y2, vec4 color=black) { line(vec2(x1,y1),vec2(x2,y2),color); }

/// Draws a disk
void disk(vec2 p, float r=1, vec4 color=black);
