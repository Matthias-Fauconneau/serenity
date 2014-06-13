#pragma once
#include "matrix.h"
#include "opencl.h"

struct Projection {
    Projection(int3 volumeSize, int3 projectionSize, uint index);
    mat4 worldToView; // Transforms from world coordinates [±1] to view coordinates [±size/2, 1] (cannot directly transform to image coordinates because of perspective division)
    mat4 imageToWorld; // Transforms from image coordinates (origin: [0,0,0,1], ray: [size,1,0]) to world coordinates [±1/2]
};

// -- Projection --

/// Projects \a volume into \a image according to \a projection
uint64 project(const ImageF& image, const CLVolume& volume, const uint projectionCount, const uint index);

/// Projects (A) \a x to \a Ax
uint64 project(const ImageArray& Ax, const CLVolume& x);

// -- Backprojection --

// Projects voxel coordinates to image coordinates for bilinear sample
typedef CLBuffer<mat4> ProjectionArray;

/// Backprojects (At) \a b to \a Atb
uint64 backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b);
