#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "display.h"
#include "function.h"

typedef functor<vec4(vec3)> Shader;
struct Flat : Shader {
    vec4 color;
    Flat(vec4 color):color(color){}
    Flat(byte4 color):color(vec4(color)/255.f){}
    virtual vec4 operator()(vec3) const override { return color; }
};

/// 3D rasterizer
struct Rasterizer {
    int width,height,stride;
    float* zbuffer;
    vec4* framebuffer; //RGBx
    Rasterizer(int width, int height);
    ~Rasterizer();

    void clear();

    inline void shade(float X, float Y, uint mask, const Shader& shader);

    /// Draws a triangle
    void triangle(vec4 A, vec4 B, vec4 C, const Shader& shader=Flat(black));
    /// Draws a quad
    inline void quad(vec4 A, vec4 B, vec4 C, vec4 D, const Shader& shader=Flat(black)) {
        triangle(A,B,C,shader);
        triangle(C,D,A,shader);
    }

    /// Draws a circle
    void circle(vec4 A, float r, const Shader& shader=Flat(black));

    /// Draws a thick line (trapezoid) from \a a to \a b with width interpolated from \a wa to \a wb
    void line(vec4 a, vec4 b, float wa=1, float wb=1, const Shader& shader=Flat(black));
    /// Draws a thick line (rectangle) from \a a to \a b
    inline void line(vec4 a, vec4 b, float w=1, const Shader& shader=Flat(black)) { line(a,b,w,w,shader); }

    void resolve(int2 position, int2 size);
};
