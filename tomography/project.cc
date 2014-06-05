#include "project.h"
#include "volume.h"
#include "opencl.h"

KERNEL(project, project) //float3 origin, float2 plusMinusHalfHeightMinusOriginZ, float3 rayX, float3 rayY, float3 ray1, float c /*origin.xy² - r²*/, float radiusSq, float halfHeight, float3 dataOrigin, __read_only image3d_t volume, sampler_t volumeSampler, const uint width, __global float* image

/// Projects \a volume onto \a image according to \a projection
void project(cl_mem buffer, size_t bufferOffset, int2 imageSize, const VolumeF& volume, const Projection& p) {
    // Cylinder parameters
    const float radius = float(volume.size.x-1)/2;
    const float halfHeight = float(volume.size.z-1 -1 )/2;  //(N-1 [domain size] - epsilon)
    float3 center = {radius, radius, halfHeight};
    // Executes projection kernel
    static cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    float3 origin = p.imageToWorld[3].xyz();
    setKernelArgs(projectKernel, p.imageToWorld, float2(1,-1) * halfHeight - origin.z, sq(origin.xy()) - sq(radius), sq(radius), halfHeight, float4(center + origin,0), volume.data.pointer, sampler, bufferOffset, imageSize.x, buffer);
    clCheck( clEnqueueNDRangeKernel(queue, projectKernel, 2, 0, (size_t[2]){size_t(imageSize.x), size_t(imageSize.y)}, 0, 0, 0, 0), "project");
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
