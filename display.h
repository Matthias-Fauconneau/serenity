#pragma once
/// \file display.h 2D graphics primitives (fill, blit, substract, line)
#include "vector.h"
#include "image.h"
#include "gl.h"

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
constexpr vec4 blue __(1, 0, 0, 1);
constexpr vec4 cyan __(1, 1, 0, 1);
constexpr vec4 green __(0, 1, 0, 1);
constexpr vec4 yellow __(0, 1, 1, 1);
constexpr vec4 red __(0, 0, 1, 1);
constexpr vec4 magenta __(1, 0, 1, 1);

// Graphics primitives

/// Fills pixels inside \a rect with \a color
void fill(Rect rect, vec4 color=black);

/// Blits \a source at \a target (with per pixel opacity if \a source.alpha is set)
/// \a opacity multiplies alpha channel by opacity/255, alpha is accumulated in framebuffer
void blit(int2 target, const GLTexture& source, vec4 color=black);
inline void blit(int2 target, const Image& source, vec4 color=black) { blit(target,GLTexture(source),color); } //FIXME
/// Substracts \a source from \a target
void substract(int2 target, const GLTexture& source, vec4 color=black);
inline void substract(int2 target, const Image& source, vec4 color=black) { substract(target,GLTexture(source),color); } //FIXME

/// Draws a thin antialiased line from p1 to p2
void line(vec2 p1, vec2 p2, vec4 color=black);
inline void line(int2 p1, int2 p2, vec4 color=black) { line(vec2(p1),vec2(p2),color); }
