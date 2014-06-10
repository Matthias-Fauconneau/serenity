#pragma once
#include "matrix.h"
#include "volume.h"

struct Projection {
    Projection(int3 volumeSize, int3 projectionSize, uint index);
    mat4 worldToView; // Transforms from world coordinates [±1] to view coordinates [±size/2, 1] (cannot directly transform to image coordinates because of perspective division)
    mat4 imageToWorld; // Transforms from image coordinates (origin: [0,0,0,1], ray: [size,1,0]) to world coordinates [±1/2]
};

// -- Projection --

/// Projects \a volume into \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const uint projectionCount, const uint index);

/// Projects (A) \a x to \a Ax
void project(const ImageArray& Ax, const VolumeF& x);

// -- Backprojection --

// Projects voxel coordinates to image coordinates for bilinear sample
struct ProjectionArray {
    ProjectionArray(int3 volumeSize, int3 projectionSize);
    int size;
    handle<cl_mem> data;
};

/// Backprojects (At) \a b to \a Atb
void backproject(const VolumeF& Atb, const ProjectionArray& At, const ImageArray& b);
