#include "project.h"
#include "volume.h"
#include "opencl.h"

KERNEL(project, project) //float3 origin, float2 plusMinusHalfHeightMinusOriginZ, float3 rayX, float3 rayY, float3 ray1, float c /*origin.xy² - r²*/, float radiusSq, float halfHeight, float3 dataOrigin, __read_only image3d_t volume, sampler_t volumeSampler, const uint width, __global float* image

/// Projects \a volume onto \a image according to \a projection
void project(cl_mem buffer, uint bufferOffset, int2 size, const VolumeF& volume, const Projection& projection) {
    // Cylinder parameters
    const float radius = float(volume.size.x-1)/2;
    const float halfHeight = float(volume.size.z-1 -1 )/2;  //(N-1 [domain size] - epsilon) (-1?)
    float3 center = {radius, radius, halfHeight};
    // Projection parameters
    mat3 rotation = mat3().rotateZ( projection.angle );
    float3 offset = rotation * (radius * projection.offset);
    float pixelSize = float(volume.size.x-1)/float(size.x-1); // Pixel size in voxels on origin plane (-1?)
    float3 rayX = rotation * float3(0,pixelSize,0);
    float3 rayY = rotation * float3(0,0,pixelSize);
    float3 ray1 = rotation * (radius * float3(-offset.x,-1,-1));
    // Executes projection kernel
    static cl_kernel kernel = project();
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    setKernelArgs(kernel, offset, (float2){1,-1} * halfHeight - offset.z, rayX, rayY, ray1, sq(offset.xy()) - sq(radius), sq(radius), halfHeight, center + offset, volume.data.pointer, sampler, bufferOffset, size.x, buffer);
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, (size_t[2]){(size_t)size.x, (size_t)size.y}, 0, 0, 0, 0) );
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, image.data.size*sizeof(float), 0, 0);
    project(buffer, 0, image.size, volume, projection);
    // Transfers result
    clEnqueueReadBuffer(queue, buffer, true, 0,  image.data.size*sizeof(float), (void*)image.data.data, 0,0,0);
    clReleaseMemObject(buffer);
}
