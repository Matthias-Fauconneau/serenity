#include "project.h"
#include "opencl.h"

mat4 Projection::worldToView(uint index) const {
    // Projection setup (coordinates in view space)
    float volumeAspectRatio = float(volumeSize.z-1) / float(volumeSize.x-1);
    float projectionAspectRatio = float(projectionSize.y-1) / float(projectionSize.x-1);
    const float detectorHalfHeight = projectionAspectRatio * detectorHalfWidth; //image_height * pixel_size; // [mm] ~ 397 mm
    const float volumeRadius = detectorHalfWidth / sqrt(sq(detectorHalfWidth)+sq(cameraLength)) * specimenDistance;
    float zExtent = (specimenDistance - volumeRadius) / cameraLength * detectorHalfHeight; // Fits cap tangent intersection to detector top edge
    const float deltaZ = volumeAspectRatio - zExtent/volumeRadius;

    float angle = doubleHelix ? 2*PI*pitch*float(index/2)/((count-1)/2) + (index%2?PI:0) : 2*PI*pitch*float(index)/(count-1); // Rotation angle (in radians) around vertical axis
    float dz = doubleHelix ? float(index/2)/float((count-1)/2) : float(index)/float(count-1);
    float z = -volumeAspectRatio + zExtent/volumeRadius + 2*dz*deltaZ; // Z position in world space

    const float3 halfVolumeSize = float3(volumeSize-int3(1))/2.f;
    float distance = specimenDistance/volumeRadius; // Distance in world space
    float extent = 2/sqrt(1-1/sq(distance)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)
    return mat4().scale(vec3(vec2(float(projectionSize.x-1)/extent),1/distance)).rotateZ(PI/2).rotateY(-PI/2).translate(vec3(distance,0,z)).rotateZ(angle).scale(1.f/halfVolumeSize);
}

mat4 Projection::imageToWorld(uint index) const {
    mat4 viewToWorld = worldToView(index).inverse();
    mat4 imageToWorld = viewToWorld.scale(vec4(1,1,1,0)).translate(vec3(-vec2(projectionSize.xy()-int2(1))/2.f,0));
    imageToWorld[2] += imageToWorld[3]; // Precomputes ray origin (input will always be (x,y,1,1))
    imageToWorld[3] = viewToWorld[3]; // View origin in world coordinates (stored in unused last column)
    return imageToWorld;
}

// -- Projection

CL(project, project)

static uint64 project(const CLBufferF& buffer, const CLVolume& volume, const Projection& projection, const uint index) {
    float3 center = vec3(volume.size-int3(1))/2.f;
    mat4 imageToWorld = projection.imageToWorld(index);
    float3 origin = imageToWorld[3].xyz();
    // dataOrigin uses +1/2 offset as samples are defined to be from [1/2..size-1/2] when filtered by OpenCL CLK_FILTER_LINEAR
    //                                                    imageToWorld, plusMinusHalfHeightMinusOriginZ,                         c,                                                                     radiusSq,    halfHeight, dataOrigin
    return CL::project(projection.projectionSize.xy(), imageToWorld, float2(1,-1) * (center.z-1.f/2/*fix OOB*/) - origin.z, sq(origin.xy()) - sq(center.x) + 1 /*fix OOB*/, sq(center.x), center.z, float4(origin + center + float3(1./2),0), volume, noneLinearSampler, projection.projectionSize.x, buffer.pointer);
}

/// Projects \a volume onto \a image according to \a projection
uint64 project(const ImageF& image, const Projection& A, const CLVolume& volume, const uint index) {
    CLBufferF buffer (image.data.size);
    uint64 time = project(buffer, volume, A, index);
    buffer.read(image.data);
    return time;
}

/// Projects (A) \a x to \a Ax
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint subsetIndex, uint subsetSize, uint subsetCount) {
    assert_(subsetSize * subsetCount == uint(A.count));
    CLBufferF buffer (Ax.size.y*Ax.size.x);
    uint64 time = 0;
    for(uint index: range(subsetSize)) { //FIXME: Queue all projections at once ?
        time += ::project(buffer, x, A, interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index));
        copy(Ax, index, buffer); //FIXME: NVidia OpenCL doesn't implement writes to 3D images
    }
    return time;
}

// -- Backprojection

CL(backproject, backproject) //const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const struct mat4* worldToView, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects (At) \a b to \a Atb
uint64 backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b) {
    assert_(At.size);
    const float3 center = float3(Atb.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(b.size.xy()-int2(1))/2.f;
    // imageCenter uses +1/2 offset as samples are defined to be from [1/2..size-1/2] when filtered by OpenCL CLK_FILTER_LINEAR
    return emulateWriteTo3DImage(CL::backproject, Atb, float4(center,0), radiusSq, imageCenter + float2(1./2), uint(At.size), At.pointer, b.pointer, clampLinearSampler);
}
