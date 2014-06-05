#include "project.h"
#include "volume.h"
#include "opencl.h"

KERNEL(project, project) //float3 origin, float2 plusMinusHalfHeightMinusOriginZ, float3 rayX, float3 rayY, float3 ray1, float c /*origin.xy² - r²*/, float radiusSq, float halfHeight, float3 dataOrigin, __read_only image3d_t volume, sampler_t volumeSampler, const uint width, __global float* image

/// Projects \a volume onto \a image according to \a projection
void project(cl_mem buffer, size_t bufferOffset, int2 size, const VolumeF& volume, const Projection& projection) {
    // Cylinder parameters
    const float radius = float(volume.size.x-1)/2;
    const float halfHeight = float(volume.size.z-1 -1 )/2;  //(N-1 [domain size] - epsilon)
    float3 center = {radius, radius, halfHeight};
    // Projection parameters
    mat3 rotation = mat3().rotateZ(projection.angle);
    float3 offset = rotation * (radius * projection.offset);
    float extent = float(volume.size.x-1)/sqrt(1-1/sq(projection.offset.x)); // Projection of the tangent intersection point on the origin plane (i.e projection of the detector extent on the origin plane)
    float pixelSize = extent/float(size.x-1); // Pixel size in voxels on origin plane
    float3 rayX = rotation * float3(0,pixelSize,0);
    float3 rayY = rotation * float3(0,0,pixelSize);
    float3 ray1 = rotation * float3(-radius*projection.offset.x,-pixelSize*float(size.x-1)/2,-pixelSize*float(size.y-1)/2);
    // Executes projection kernel
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    setKernelArgs(projectKernel, float4(offset,0), float2(1,-1) * halfHeight - offset.z, float4(rayX,0), float4(rayY,0), float4(ray1,0), sq(offset.xy()) - sq(radius), sq(radius), halfHeight, float4(center + offset,0), volume.data.pointer, sampler, bufferOffset, size.x, buffer);
    clCheck( clEnqueueNDRangeKernel(queue, projectKernel, 2, 0, (size_t[2]){size_t(size.x), size_t(size.y)}, 0, 0, 0, 0), "project");
    /*{
        ::buffer<float> data (size.x * size.y);
        clCheck( clEnqueueReadBuffer(queue, buffer, true, 0, size.x*size.y*sizeof(float), (float*)data.data, 0,0,0) );
        for(float x: data) assert_(isNumber(x), x);
    }*/
}

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY, image.data.size*sizeof(float), 0, 0);
    project(buffer, 0, image.size, volume, projection);
    // Transfers result
    clEnqueueReadBuffer(queue, buffer, true, 0,  image.data.size*sizeof(float), (float*)image.data.data, 0,0,0);
    clReleaseMemObject(buffer);
}
