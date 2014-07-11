#pragma once
#include "matrix.h"

enum Trajectory { Single, Double, Adaptive };
inline string str(const Trajectory& t) { return ref<string>({"single"_,"double"_,"adaptive"_})[int(t)]; }

/// Projection configuration
/// \note Defines all \a count projections using an index parameter
struct Projection {
    // Setup parameters
    const float detectorHalfWidth = 1;
    const float cameraLength;
    const float specimenDistance;
    // Reconstruction resolution
    const int3 volumeSize;
    const int3 projectionSize;
    const uint count = projectionSize.z;
    // Acquisition parameters
    const Trajectory trajectory;
    const float rotationCount;

    // Projection setup (coordinates in view space)
    const float volumeAspectRatio = float(volumeSize.z-1) / float(volumeSize.x-1);
    const float projectionAspectRatio = float(projectionSize.y-1) / float(projectionSize.x-1);
    const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth;
    const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
    const float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
    const float deltaZ = volumeAspectRatio - zExtent/volumeRadius; // Scales normalized Z to Z view origin in world space so that the cylinder caps exactly fits the view at the trajectory extremes
    const float distance = specimenDistance/volumeRadius; // Distance in world space
    const float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)

    Projection(float cameraLength, float specimenDistance, int3 volumeSize, int3 projectionSize, Trajectory trajectory, float rotationCount) : cameraLength(cameraLength), specimenDistance(specimenDistance), volumeSize(volumeSize), projectionSize(projectionSize), trajectory(trajectory), rotationCount(rotationCount) {}

    // Rotation angle (in radians) around vertical axis
    float angle(uint index) const {
        if(trajectory==Single || trajectory==Adaptive) return 2*PI*rotationCount*float(index)/count;
        if(trajectory==Double) return 2*PI*rotationCount*float(index%(count/2))/((count)/2) + (index/(count/2)?PI:0);
        error(int(trajectory));
    }

    // Normalized Z coordinate
    float dz(uint index) const {
        if(trajectory==Single) return float(index)/float(count-1);
        if(trajectory==Double) return float(index%(count/2))/float((count-1)/2);
        if(trajectory==Adaptive) { assert_(rotationCount==int(rotationCount)); return clip(0.f, float(int(index)-int(count/(2*rotationCount)))/float(count-count/rotationCount), 1.f); }
        error(int(trajectory));
    }

    /// Transforms from world coordinates [±size/2] to view coordinates (only rotation and translation)
    mat4 worldToView(uint index) const;
    /// Transforms from world coordinates to view coordinates (scaled to [±size/2]) (FIXME)
    mat4 worldToScaledView(uint index) const;
    /// Transforms from world coordinates [±size] to device coordinates [±size/2]
    mat4 worldToDevice(uint index) const;
};

inline mat4 Projection::worldToView(uint index) const {
    return mat4().rotateZ(PI/2).rotateY(-PI/2).translate(vec3(distance,0,(2*dz(index)-1)*deltaZ)*((volumeSize.x-1)/2.f)).rotateZ(angle(index));
}

inline mat4 Projection::worldToScaledView(uint index) const {
    return mat4().scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)).scale(vec3(1.f/((volumeSize.x-1)/2.f))) * worldToView(index);
}

inline mat4 Projection::worldToDevice(uint index) const {
    mat4 projectionMatrix; projectionMatrix(3,2) = 1;  projectionMatrix(3,3) = 0; // copies Z to W (FIXME: move scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)) from worldToView to projectionMatrix
    return projectionMatrix * worldToScaledView(index);
}
