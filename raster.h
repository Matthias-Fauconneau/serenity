#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "display.h"
#include "function.h"

/// 3D rasterizer
struct Rasterizer {
    int width,height;
    float* zbuffer;
    vec4* framebuffer; //RGBx
    Rasterizer(int width, int height)
        : width(width),height(height), zbuffer(allocate16<float>(width*height)), framebuffer(allocate16<vec4>(width*height)){}
    ~Rasterizer() { unallocate(zbuffer,width*height); unallocate(framebuffer,width*height); }

    void clear();

    // TODO: coverage masks
    typedef functor<vec4(vec3)> Shader;
    struct Flat : Shader { vec4 color; Flat(byte4 color):color(color){} vec4 operator()(vec2) const { return vec4(color)/255.f; } };

    /// Draws a convex polygon
    template<uint N> void polygon(vec3 polygon[N], const Shader& shader=Flat(black));
    /// Draws a triangle
    inline void triangle(vec3 A, vec3 B, vec3 C, const Shader& shader=Flat(black)) { polygon<3>((vec3[]){A,B,C},shader); }
    /// Draws a convex quad
    inline void quad(vec3 A, vec3 B, vec3 C, vec3 D, const Shader& shader=Flat(black)) { polygon<4>((vec3[]){A,B,C,D},shader); }

    /// Draws a circle
    void circle(vec3 A, float r, const Shader& shader=Flat(black));

    /// Draws a thick line (trapezoid) from \a a to \a b with width interpolated from \a wa to \a wb
    void line(vec3 a, vec3 b, float wa=1, float wb=1, const Shader& shader=Flat(black));
    /// Draws a thick line (rectangle) from \a a to \a b
    inline void line(vec3 a, vec3 b, float w=1, const Shader& shader=Flat(black)) { line(a,b,w,w,shader); }

    bool ztest(int x, int y, float z) { return z>zbuffer[y*width+x]; }
    void output(int x, int y, float z, vec4 bgra) {
        zbuffer[y*width+x] = z;
        vec4& d = framebuffer[y*width+x];
        float a = bgra[3];
        d = a*bgra+(1-a)*d;
    }

    void resolve(int2 position, int2 size);
};
