#include "project.h"
#include "opencl.h"

// -- Projection

CL(project, project)

static uint64 project(const CLBufferF& buffer, const Projection& projection, const CLVolume& volume, const uint index) {
    float3 center = vec3(volume.size-int3(1))/2.f;
    mat4 viewToWorld = projection.worldToScaledView(index).inverse();  // view coordinates [±size/2] to world coordinates [±1]
    float3 origin = viewToWorld[3].xyz(); // imageToWorld * vec2(size/2, 0, 1)
    mat4 imageToWorld = viewToWorld; // image coordinates [size, 1] to world coordinates [±1]
    imageToWorld[2] += imageToWorld * vec4(-vec2(projection.projectionSize.xy()-1)/2.f,0,0); // Stores view to image coordinates translation in imageToWorld[2] as we know it will be always be called with z=1
    imageToWorld[3] = vec4(origin, 1); // Stores origin in 4th column unused by ray direction transformation (imageToWorld*(x,y,1,0)), allows to get origin directly as imageToWorld*(0,0,0,1) instead of imageToWorld*(size/2,0,1)
    // dataOrigin uses +1/2 offset as samples are defined to be from [1/2..size-1/2] when filtered by OpenCL CLK_FILTER_LINEAR
    //                                                                          imageToWorld, plusMinusHalfHeightMinusOriginZ,                         c,                                                                     radiusSq,    halfHeight, dataOrigin
    return CL::project(projection.projectionSize.xy(), imageToWorld, float2(1,-1) * (center.z-1.f/2/*fix OOB*/) - origin.z, sq(origin.xy()) - sq(center.x) + 1 /*fix OOB*/, sq(center.x), center.z, float4(origin + center + float3(1./2),0), volume, noneLinearSampler, projection.projectionSize.x, buffer.pointer);
}

/// Projects (A) \a x to \a Ax
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint index) {
    CLBufferF buffer (Ax.size.y*Ax.size.x);
    uint64 time = ::project(buffer, A, x, index);
    copy(Ax, index, buffer); //FIXME: NVidia OpenCL doesn't implement writes to 3D images
    return time;
}

/// Projects \a volume onto \a image according to \a projection
uint64 project(const ImageF& image, const Projection& A, const CLVolume& volume, const uint index) {
    CLBufferF buffer (image.data.size);
    uint64 time = project(buffer, A, volume, index);
    buffer.read(image.data);
    return time;
}

/// Projects (A) \a x to \a Ax
uint64 project(const ImageArray& Ax, const Projection& A, const CLVolume& x, uint subsetIndex, uint subsetSize, uint subsetCount) {
    assert_(uint(Ax.size.z) == subsetSize && subsetSize * subsetCount == uint(A.count));
    CLBufferF buffer (Ax.size.y*Ax.size.x);
    uint64 time = 0;
    for(uint index: range(subsetSize)) { //FIXME: Queue all projections at once ?
        time += ::project(buffer, A, x, interleave(subsetSize, subsetCount, subsetIndex*subsetSize+index));
        copy(Ax, index, buffer); //FIXME: NVidia OpenCL doesn't implement writes to 3D images
    }
    return time;
}

// -- Backprojection

CL(backproject, backproject) //const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const struct mat4* worldToDevice, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects (At) \a b to \a Atb
uint64 backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b) {
    assert_(At.size);
    const float3 center = float3(Atb.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(b.size.xy()-int2(1))/2.f;
    // imageCenter uses +1/2 offset as samples are defined to be from [1/2..size-1/2] when filtered by OpenCL CLK_FILTER_LINEAR
    return emulateWriteTo3DImage(CL::backproject, Atb, float4(center,0), radiusSq, imageCenter + float2(1./2), uint(At.size), At.pointer, b.pointer, clampLinearSampler);
}
