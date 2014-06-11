#include "project.h"
#include "volume.h"
#include "opencl.h"

Projection::Projection(int3 volumeSize, int3 projectionSize, uint index) {
    // Projection setup (coordinates in view space)
    const float detectorHalfWidth = 1;
    const float cameraLength = 1;
    const float specimenDistance = 1./16;
    float volumeAspectRatio = float(volumeSize.z-1) / float(volumeSize.x-1);
    float projectionAspectRatio = float(projectionSize.y-1) / float(projectionSize.x-1);
    const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth; //image_height * pixel_size; // [mm] ~ 397 mm
    const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
    float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
    const float deltaZ = volumeAspectRatio - zExtent/volumeRadius;

    float angle = 2*PI*float(index)/projectionSize.z; // Rotation angle (in radians) around vertical axis
    float distance = specimenDistance/volumeRadius; // Distance in world space
    float z = -volumeAspectRatio + zExtent/volumeRadius + 2*float(index)/float(projectionSize.z)*deltaZ; // Z position in world space

    const float3 halfVolumeSize = float3(volumeSize-int3(1))/2.f;
    float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)
    worldToView = mat4().scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)).rotateZ(PI/2).rotateY(-PI/2).translate(vec3(distance,0,z)).rotateZ(angle).scale(1.f/halfVolumeSize);
    mat4 viewToWorld = worldToView.inverse();
    imageToWorld = viewToWorld.scale(vec4(1,1,1,0)).translate(vec3(-vec2(projectionSize.xy()-int2(1))/2.f,0));
    imageToWorld[2] += imageToWorld[3]; // Precomputes ray origin (input will always be (x,y,1,1))
    imageToWorld[3] = viewToWorld[3]; // View origin in world coordinates (stored in unused last column)
}

// -- Projection

CL(project, project)

static void project(const CLBufferF& buffer, int3 imageSize, const CLVolume& volume, const uint index) {
    const float radius = float(volume.size.x-1)/2;
    const float halfHeight = float(volume.size.z-1 -1 )/2;  //(N-1 [domain size] - epsilon)
    float3 center = {radius, radius, halfHeight};
    mat4 imageToWorld = Projection(volume.size, imageSize, index).imageToWorld;
    float3 origin = imageToWorld[3].xyz();
    CL::project(imageSize.xy(), imageToWorld, float2(1,-1) * halfHeight - origin.z, sq(origin.xy()) - sq(radius), sq(radius), halfHeight, float4(center + origin,0), volume, clampToEdgeSampler, imageSize.x, buffer);
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const CLVolume& volume, const uint projectionCount, const uint index) {
    CLBufferF buffer (image.data.size);
    project(buffer, int3(image.size, projectionCount), volume, index);
    buffer.read(image.data);
}

/// Projects (A) \a x to \a Ax
void project(const ImageArray& Ax, const CLVolume& x) {
    CLBufferF buffer (Ax.size.y*Ax.size.x);
    for(uint index: range(Ax.size.z)) { //FIXME: Queue all projections at once ?
        ::project(buffer, Ax.size, x, index);
        copy(Ax, index, buffer); //FIXME: NVidia OpenCL doesn't implement writes to 3D images
    }
}

/// Projects (A) \a x to \a Ax
void project(const VolumeF& Ax, const CLVolume& x) {
    for(uint index: range(Ax.size.z)) ::project(slice(Ax, index), x, Ax.size.z, index); //FIXME: Queue all projections at once ?
}

// -- Backprojection

CL(backproject, backproject) //const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const struct mat4* worldToView, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects (At) \a b to \a Atb
void backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b) {
    assert_(At.size);
    const float3 center = float3(Atb.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(b.size.xy()-int2(1))/2.f;
    CL::backproject(Atb.size, float4(center,0), radiusSq, imageCenter, At.size, At, b, clampSampler, Atb);
}
