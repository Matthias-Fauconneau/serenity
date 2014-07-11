#pragma once
#include "matrix.h"
#include "projection.h"
#include "opencl.h"

/// Interleaves \a index so that the local subset index becomes most significant
inline uint interleave(const uint subsetSize, const uint subsetCount, const uint index) {
    const uint subsetIndex = index / subsetSize, localIndex = index % subsetSize;
    return localIndex * subsetCount + subsetIndex;
}

// -- Projection --

/// Projects (\a Ax = \a A \a x) projection index and copy to host memory
uint64 project(const ImageF& Ax, const Projection& A, const CLVolume& volume, const uint index);

/// Projects (\a Ax = \a A \a x) projection \a index to slice \a index
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint index);

/// Projects (\a Ax = \a A \a x) interleaved projections to contiguous slices
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint subsetIndex, uint subsetSize, uint subsetCount);
/// Projects (\a Ax = \a A \a x) all contiguous projections to contiguous slices
inline uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x) { return project(Ax, A, x, 0, Ax.size.z, 1); }

// -- Backprojection --

/// Backprojects \a b to \a Atb
/// \param Atb Target volume to store the backprojection
/// \param At Array of matrix for each index projecting voxel coordinates to image coordinates
/// \param b Source volume containing the projections images corresponding to \a At
uint64 backproject(const CLVolume& Atb, const CLBuffer<mat4>& At, const ImageArray& b);
