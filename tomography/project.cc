#include "project.h"
#include "thread.h"
#include "opencl.h"

KERNEL(project, project)

struct CLProjection {
 float4 origin;
 float4 plusMinusHalfHeightMinusOriginZ; // halfHeight - origin.z
 float4 ray[3];
 float c; // origin.xy² - r²
 float radiusSq;
 float halfHeight;
 float pad;
 float4 dataOrigin; // origin + (volumeSize-1)/2
};

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection) {
    static cl_kernel kernel = project();

    float radius = float(volume.sampleCount.x-1)/2;
    float halfHeight = float(volume.sampleCount.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
    float3 dataOrigin = {float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1 -1 /*FIXME*/)/2};
    CLProjection clProjection = {{projection.origin,0}, {(float2){1,-1} * halfHeight - projection.origin.z,0,0}, {{projection.ray[0],0}, {projection.ray[1],0}, {projection.ray[2],0}}, sq(projection.origin.xy()) - radius*radius, radius*radius, halfHeight, 0, {projection.origin + dataOrigin,0}};
    clSetKernelArg(kernel, 0, sizeof(CLProjection), &clProjection);

    clSetKernelArg(kernel, 1, sizeof(volume.data.pointer), &volume.data.pointer);

    cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    clSetKernelArg(kernel, 2, sizeof(sampler), &sampler);

    clSetKernelArg(kernel, 3, sizeof(image.width), &image.width);

    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY, image.data.size*sizeof(float), 0, 0);
    //void* imageMap = clEnqueueMapBuffer(queue, clImage, true, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 4, sizeof(clImage), &clImage);

    size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0, 0, 0, 0) );

    clEnqueueReadBuffer(queue, clImage, true, 0,  image.data.size*sizeof(float), (void*)image.data.data, 0,0,0);
    clReleaseMemObject(clImage);
}
