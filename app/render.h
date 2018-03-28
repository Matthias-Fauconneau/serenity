#pragma once
#include "image.h"
#include "matrix.h"
#include "mwc.h"

struct Scene {
    struct Quad {
        uint4 quad;
        Image3f outgoingRadiance; // for real surfaces: differential
        bool real = true;
        Image3f realOutgoingRadiance; // Faster rendering of synthetic real surfaces for synthetic test
        //const bgr3f albedo = 1;
    };

    array<vec3> vertices;
    array<Quad> quads;

    struct QuadLight { vec3 O, T, B, N; bgr3f emissiveFlux; };

    const float lightSize = 1;
    Scene::QuadLight light {{-lightSize/2,-lightSize/2,2}, {lightSize,0,0}, {0,lightSize,0}, {0,0,-sq(lightSize)}, bgr3f(2)/*lightSize/sq(lightSize)*/};
};

struct Render {
    Scene scene;
    Random random;

    Render();
    void render(const Image& target, const mat4 view, const mat4 projection, const Image8& realImage);
};
