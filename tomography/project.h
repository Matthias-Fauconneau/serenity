#pragma once
#include "matrix.h"
#include "opencl.h"

struct Projection {
    float3 offset; // Offset of X-ray point source from reconstruction volume center (in normalized coordinates)
    float angle; // Rotation angle (in radians) around vertical axis
};

/// Projects \a volume into \a buffer according to \a projection
void project(cl_mem buffer, uint bufferOffset, int2 size, const struct VolumeF& volume, const Projection& projection);

/// Projects \a volume into \a image according to \a projection
void project(const struct ImageF& image, const struct VolumeF& volume, const Projection& projection);
