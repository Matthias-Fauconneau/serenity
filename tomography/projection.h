#pragma once
#include "matrix.h"
#include "image.h"

// Projection settings
struct Projection {
    // Resolution
    int3 volumeSize;
    int3 projectionSize;
    uint count = projectionSize.z;
    // Parameters
    float detectorHalfWidth = 1;
    float cameraLength = 1;
    float specimenDistance = 1./16;
    enum Trajectory { Single, Double, Adaptive } trajectory;
    uint rotationCount;
    float photonCount; // Photon count per pixel for a blank scan (without attenuation) of same duration (0: no noise)

    // Projection setup (coordinates in view space)
    const float volumeAspectRatio = float(volumeSize.z-1) / float(volumeSize.x-1);
    const float projectionAspectRatio = float(projectionSize.y-1) / float(projectionSize.x-1);
    const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth; //image_height * pixel_size; // [mm] ~ 397 mm
    const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
    const float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
    const float deltaZ = volumeAspectRatio - zExtent/volumeRadius; // Scales normalized Z to Z view origin in world space so that the cylinder caps exactly fits the view at the trajectory extremes
    const float distance = specimenDistance/volumeRadius; // Distance in world space
    const float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)

    Projection(int3 volumeSize, int3 projectionSize, const Trajectory trajectory, const uint rotationCount, const float photonCount = 0) : volumeSize(volumeSize), projectionSize(projectionSize), trajectory(trajectory), rotationCount(rotationCount), photonCount(photonCount) {}

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
        if(trajectory==Adaptive) return clip(0.f, float(int(index)-int(count/(2*rotationCount)))/float(count-count/rotationCount), 1.f);
        error(int(trajectory));
    }

    // Transforms from world coordinates [±size/2] to view coordinates (only rotation and translation)
    mat4 worldToView(uint index) const;
    // Transforms from world coordinates to view coordinates (scaled to [±size/2]) (FIXME)
    mat4 worldToScaledView(uint index) const;
    // Transforms from world coordinates [±size] to device coordinates [±size/2]
    mat4 worldToDevice(uint index) const; };

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

inline String str(const Projection& A) { return str(strx(A.volumeSize), strx(A.projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(A.trajectory)], A.rotationCount, A.photonCount); }
