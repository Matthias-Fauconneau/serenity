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

KERNEL(project, project)

static void project(cl_mem buffer, size_t bufferOffset, int3 imageSize, const CLVolume& volume, const uint index) {
    // Cylinder parameters
    const float radius = float(volume.size.x-1)/2;
    const float halfHeight = float(volume.size.z-1 -1 )/2;  //(N-1 [domain size] - epsilon)
    float3 center = {radius, radius, halfHeight};
    // Executes projection kernel
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    mat4 imageToWorld = Projection(volume.size, imageSize, index).imageToWorld;
    float3 origin = imageToWorld[3].xyz();
    setKernelArgs(projectKernel, imageToWorld, float2(1,-1) * halfHeight - origin.z, sq(origin.xy()) - sq(radius), sq(radius), halfHeight, float4(center + origin,0), volume.data.pointer, sampler, bufferOffset, imageSize.x, buffer);
    clCheck( clEnqueueNDRangeKernel(queue, projectKernel, 2, 0, (size_t[2]){size_t(imageSize.x), size_t(imageSize.y)}, 0, 0, 0, 0), "project",
            imageToWorld, float2(1,-1) * halfHeight - origin.z, sq(origin.xy()) - sq(radius), sq(radius), halfHeight, float4(center + origin,0), volume.data.pointer, sampler, bufferOffset, imageSize.x, buffer);
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const CLVolume& volume, const uint projectionCount, const uint index) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, image.data.size*sizeof(float), 0, 0);
    project(buffer, 0, int3(image.size, projectionCount), volume, index);
    // Transfers result
    clEnqueueReadBuffer(queue, buffer, true, 0,  image.data.size*sizeof(float), (float*)image.data.data, 0,0,0);
    clReleaseMemObject(buffer);
}

/// Projects (A) \a x to \a Ax
void project(const ImageArray& Ax, const CLVolume& x) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, Ax.size.y*Ax.size.x*sizeof(float), 0, 0);
    for(uint index: range(Ax.size.z)) { //FIXME: Queue all projections at once ?
        ::project(buffer, 0, Ax.size, x, index);
        clEnqueueCopyBufferToImage(queue, buffer, Ax.data.pointer, 0, (size_t[]){0,0,index}, (size_t[]){size_t(Ax.size.x),size_t(Ax.size.y),size_t(1)}, 0,0,0); //FIXME: NVidia OpenCL doesn't implement writes to 3D images
    }
    clReleaseMemObject(buffer);
}

// -- Backprojection

ProjectionArray::ProjectionArray(int3 volumeSize, int3 projectionSize) : size(projectionSize.z) {
    buffer<mat4> worldToViews (size);
    for(uint index: range(size)) worldToViews[index] = Projection(volumeSize, projectionSize, index).worldToView;
    data.pointer = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, worldToViews.size*sizeof(mat4), (float*)worldToViews.data, 0);
    assert_(data.pointer);
}

KERNEL(backproject, backproject) //const float3 center, const float radiusSq, const float2 imageCenter, const size_t projectionCount, const struct mat4* worldToView, read_only image3d_t images, sampler_t imageSampler, image3d_t Y

/// Backprojects (At) \a b to \a Atb
void backproject(const CLVolume& Atb, const ProjectionArray& At, const ImageArray& b) {
    const float3 center = float3(Atb.size-int3(1))/2.f;
    const float radiusSq = sq(center.x);
    const float2 imageCenter = float2(b.size.xy()-int2(1))/2.f;
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP, CL_FILTER_LINEAR, 0); // NVidia does not implement OpenCL 1.2 (2D image arrays)
    setKernelArgs(backprojectKernel, float4(center,0), radiusSq, imageCenter, size_t(At.size), At.data.pointer, b.data.pointer, sampler, Atb.data.pointer);
    clCheck( clEnqueueNDRangeKernel(queue, backprojectKernel, 3, 0, (size_t[]){size_t(Atb.size.x), size_t(Atb.size.y), size_t(Atb.size.z)}, 0, 0, 0, 0), "backproject");
}

