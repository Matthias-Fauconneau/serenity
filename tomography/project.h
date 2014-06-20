#pragma once
#include "matrix.h"
#include "opencl.h"

inline uint interleave(const uint subsetSize, const uint subsetCount, const uint index) {
    const uint subsetIndex = index / subsetSize, localIndex = index % subsetSize;
    return localIndex * subsetCount + subsetIndex;
}

// Projection settings
struct Projection {
    int3 volumeSize;
    int3 projectionSize;
    uint count = projectionSize.z;

    float detectorHalfWidth = 1;
    float cameraLength = 1;
    float specimenDistance = 1./16;
    bool doubleHelix;
    float pitch;
    const float photonCount = 0; // Photon count per pixel for a blank scan (without attenuation) of same duration (0: no noise)

    Projection(int3 volumeSize, int3 projectionSize, const bool doubleHelix = false, const float pitch = 1) : volumeSize(volumeSize), projectionSize(projectionSize), doubleHelix(doubleHelix), pitch(pitch) {}

    // Transforms from world coordinates [±1] to view coordinates [±size/2, 1] (cannot directly transform to image coordinates because of perspective division)
    mat4 worldToView(uint index) const;
    // Transforms from image coordinates (origin: [0,0,0,1], ray: [size,1,0]) to world coordinates [±1/2]
    mat4 imageToWorld(uint index) const;
};

// -- Projection --

/// Projects \a volume into \a image according to \a projection
uint64 project(const ImageF& image, const Projection& A, const CLVolume& volume, const uint index);

/// Projects (A) \a x to \a Ax
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint subsetIndex, uint subsetSize, uint subsetCount);
inline uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x) { return project(Ax, A, x, 0, Ax.size.z, 1); }

// -- Backprojection --

// Projects voxel coordinates to image coordinates for bilinear sample
typedef CLBuffer<mat4> ProjectionArray;

/// Backprojects (At) \a b to \a Atb
uint64 backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b);
