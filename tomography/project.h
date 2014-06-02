#pragma once
#include "volume.h"
#include "matrix.h"

struct Projection {
    int3 volumeSize; int2 imageSize; uint index;
    Projection(const int3 volumeSize, const int2 imageSize, const uint index, const uint projectionCount) : volumeSize(volumeSize), imageSize(imageSize), index(index) {
#if 0
        // FIXME: parse from measurement file
        const uint image_height = 1536;
        assert_(image_height*imageSize.x == image_width*imageSize.y, imageSize, image_height, image_width);
        const uint num_projections_per_revolution = 2520;
        const float z_start_position = 32.1; // [mm]
        const float z_end_position = 37.3082; // [mm]
#else // Synthetic
        const uint image_width = 2048;
        const uint num_projections_per_revolution = projectionCount/2;
        const float z_start_position = 0; // [mm]
#endif
        const float pixel_size = 0.194; // [mm]
        const float detectorHalfWidth = image_width * pixel_size; // [mm] ~ 397 mm
        const float camera_length = 328.811; // [mm]
        const float hFOV = atan(detectorHalfWidth, camera_length); // Horizontal field of view (i.e cone beam angle) [rad] ~ 50°
        const float specimen_distance = 2.78845; // [mm]
        const float volumeRadius = specimen_distance * cos(hFOV); // [mm] ~ 2 mm
        const float voxelRadius = float(volumeSize.x-1)/2;
#if 1 // Synthetic
        const float z_end_position = volumeSize.z*volumeRadius/voxelRadius; // [mm]
#endif
        const float deltaZ = (z_end_position-z_start_position) /*/ 2*/ /* Half pitch: Z[end] is incorrect ?*/; // [mm] ~ 5 mm

        mat3 rotation = mat3().rotateZ(2*PI*float(index)/num_projections_per_revolution);
        origin = rotation * vec3(-specimen_distance/volumeRadius*voxelRadius,0, (float(index)/float(projectionCount)*deltaZ)/volumeRadius*voxelRadius - volumeSize.z/2 + imageSize.y*voxelRadius/float(imageSize.x-1));
        ray[0] = rotation * vec3(0,2.f*voxelRadius/float(imageSize.x-1),0);
        ray[1] = rotation * vec3(0,0,2.f*voxelRadius/float(imageSize.x-1));
        ray[2] = rotation * vec3(specimen_distance/volumeRadius*voxelRadius,0,0) - vec3((imageSize.x-1)/2.f)*ray[0] - vec3((imageSize.y-1)/2.f)*ray[1];
        float scale = float(imageSize.x-1)/voxelRadius;
        this->projection = mat3().scale(vec3(1,scale,scale)).rotateZ( - 2*PI*float(index)/num_projections_per_revolution);
    }
    //inline vec2 project(vec3 p) const { p = project * (p - origin); return vec2(p.y / p.x, p.z / p.x); /*Perspective divide* }
    vec3 origin;
    mat3 ray;
    mat3 projection;
};

inline buffer<Projection> evaluateProjections(int3 reconstructionSize, int2 imageSize, uint projectionCount, uint stride=1) {
    buffer<Projection> projections (projectionCount / stride);
    for(int index: range(projections.size)) projections[index] = Projection(reconstructionSize, imageSize, index*stride, projectionCount);
    return projections;
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection);
