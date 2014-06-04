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

inline buffer<Projection> evaluateProjections(int3 volumeSize, int2 imageSize, uint projectionCount, uint stride, bool synthetic) {
    buffer<Projection> projections (projectionCount / stride);
    for(int index: range(projections.size)) {
        float volumeAspectRatio = float(volumeSize.z-1) / float(volumeSize.x-1);
        float projectionAspectRatio = float(imageSize.y-1) / float(imageSize.x-1);
        // FIXME: parse from measurement file
        const uint image_width = 2048, image_height = 1536;
        if(!synthetic) assert_(image_height*imageSize.x == image_width*imageSize.y, imageSize, image_height, image_width);
        const float pixel_size = 0.194; // [mm]
        const float detectorHalfWidth = image_width * pixel_size; // [mm] ~ 397 mm
        const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth; //image_height * pixel_size; // [mm] ~ 397 mm
        const float cameraLength = 328.811; // [mm]
        const float specimenDistance = 2.78845; // [mm]
        const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
        //float xExtent = detectorHalfWidth / cameraLength * specimenDistance / volumeRadius; // Projection of the detector extent on the origin plane
        float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge

        const float deltaZ = synthetic ? volumeAspectRatio - zExtent/volumeRadius : (37.3082-32.1)/volumeRadius /*/ 2*/; // Half pitch
        const uint num_projections_per_revolution = synthetic ? projectionCount : 2520;
        projections[index] = {vec3(-specimenDistance/volumeRadius,0, -volumeAspectRatio + zExtent/volumeRadius + 2*float(index*stride)/float(projectionCount)*deltaZ), float(2*PI*float(index*stride)/num_projections_per_revolution)};
    }
    return projections;
}
