#pragma once
#include "vector.h"
#include "map.h"

struct Scene {
    /*const*/ size_t quadCount;
    /*const*/ float near, far;
    Scene(const size_t quadCount, const float near, const float far) : quadCount(quadCount), near(near), far(far) {}
    default_move(Scene);
    /*const*/ size_t quadCapacity = quadCount/*align(8, quadCount)*/;

    // Quad attributes
    buffer<float> x {quadCapacity*6, quadCount*6};
    buffer<float> y {quadCapacity*6, quadCount*6};
    buffer<float> z {quadCapacity*6, quadCount*6};

    buffer<rgb3f> diffuseReflectance {quadCount};
#ifndef JSON
    buffer<rgb3f> emissiveFlux {quadCount};
#if 1
    buffer<rgba4f> emissiveRadiance {quadCount};
#endif
#endif
    buffer<rgba4f> specularReflectance {quadCount};

    buffer<uint> sampleBase {quadCapacity, quadCount};
    buffer<uint> sampleRadianceBase {quadCapacity, quadCount};
    buffer<uint> nX {quadCapacity, quadCount};
    buffer<uint> nY {quadCapacity, quadCount};
    buffer<uint> nXω {quadCapacity, quadCount};
    buffer<uint> nYω {quadCapacity, quadCount};

    buffer<vec3> T {quadCapacity, quadCount};
    buffer<vec3> B {quadCapacity, quadCount};
    buffer<vec3> N {quadCapacity, quadCount};

    buffer<vec3> halfSizeT {quadCapacity, quadCount};
    buffer<vec3> halfSizeB {quadCapacity, quadCount};
    buffer<float> sampleArea {quadCapacity, quadCount};

    uint sampleRadianceCount = 0;
    buffer<vec3> samplePositions;

    struct QuadLight {
        vec3 O, T, B, N;
        vec2 size;
        rgb3f emissiveFlux;
    };
    buffer<QuadLight> quadLights;

    // Radiosity
    const size_t occluderQuadCapacity = align(8, quadCount);
    buffer<float> X[3] {{occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}};
    buffer<float> Y[3] {{occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}};
    buffer<float> Z[3] {{occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}, {occluderQuadCapacity*2, quadCount*2}};
};

Scene loadScene(string name, map<string,float> arguments);
