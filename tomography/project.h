#pragma once
#include "matrix.h"
#include "opencl.h"

struct Projection {
    Projection(int3 volumeSize, int2 imageSize, float angle /*Rotation angle (in radians) around vertical axis*/, float distance /*normalized by radius*/, float z /*normalized by radius*/) {
        const float3 halfVolumeSize = float3(volumeSize-int3(1))/2.f;
        float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)
        worldToView = mat4().scale(vec3(vec2(float(imageSize.x-1)/extent),1/distance)).rotateZ(PI/2).rotateY(-PI/2).translate(vec3(distance,0,z)).rotateZ(angle).scale(1.f/halfVolumeSize);
        mat4 viewToWorld = worldToView.inverse();
        imageToWorld = viewToWorld.scale(vec4(1,1,1,0)).translate(vec3(-vec2(imageSize-int2(1))/2.f,0));
        imageToWorld[2] += imageToWorld[3]; // Precomputes ray origin (input will always be (x,y,1,1))
        imageToWorld[3] = viewToWorld[3]; // View origin in world coordinates (stored in unused last column)
    }
    mat4 worldToView; // Transforms from world coordinates [±1] to view coordinates [±size/2, 1] (cannot directly transform to image coordinates because of perspective division)
    mat4 imageToWorld; // Transforms from image coordinates (origin: [0,0,0,1], ray: [size,1,0]) to world coordinates [±1/2]
};

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
        float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
        const float deltaZ = synthetic ? volumeAspectRatio - zExtent/volumeRadius : (37.3082-32.1)/volumeRadius /*/ 2*/; // Half pitch
        const uint num_projections_per_revolution = synthetic ? projectionCount : 2520;
        projections[index] = Projection(volumeSize, imageSize, float(2*PI*float(index*stride)/num_projections_per_revolution), specimenDistance/volumeRadius, -volumeAspectRatio + zExtent/volumeRadius + 2*float(index*stride)/float(projectionCount)*deltaZ);
    }
    return projections;
}

/// Projects \a volume into \a buffer according to \a projection
void project(cl_mem buffer, size_t bufferOffset, int2 size, const struct VolumeF& volume, const Projection& projection);

/// Projects \a volume into \a image according to \a projection
void project(const struct ImageF& image, const struct VolumeF& volume, const Projection& projection);
