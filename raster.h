#pragma once
/// \file raster.h 3D rasterizer (polygon, circle)
#include "display.h"
#include "function.h"

template<int N> using Shader = functor<vec4(float[N])>;

/// 3D rasterizer
struct Rasterizer {
    int width,height,stride;
    float* zbuffer;
    vec4* framebuffer; //RGBx
    Rasterizer(int width, int height);
    ~Rasterizer();

    void clear(vec4 color=vec4(1,1,1,1), float depth=-0x1p16f);

    template<int N=1> void triangle(const Shader<N>& shader, vec4 A, vec4 B, vec4 C, vec3 attributes[N]);

    void resolve(int2 position, int2 size);
};
