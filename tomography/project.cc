#include "project.h"
#include "thread.h"

#define CL 1
#if CL
#include "opencl.h"

KERNEL(project)
static cl_kernel kernel = project();

struct CLProjection {
    float3 origin;
    float _1mz; // 1 - origin.z
    float3 ray[3];
    float c; // origin.xy² - 1
};

void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    int3 size = source.sampleCount;
    float3 volumeSize = {(float)size.x,(float)size.y,(float)size.z};

    CLProjection clProjection = {projection.origin, volumeSize.z/2 - projection.origin.z, {projection.ray[0],projection.ray[1],projection.ray[2]}, sq(projection.origin.xy)-sq(volumeSize.xy)};
    clSetKernelArg(kernel, 0, sizeof(CLProjection), &clProjection);

    clSetKernelArg(kernel, 1, sizeof(volumeSize), &volumeSize);

    cl_image_format format = {CL_R, CL_FLOAT};
    cl_image_desc desc = {CL_MEM_OBJECT_IMAGE3D, (size_t)size.x, (size_t)size.y, (size_t)size.z, 0,0,0,0,0,0};
    cl_mem clSource = clCreateImage(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, &format, &desc, source.data, 0);
    void* volumeMap = clEnqueueMapBuffer(queue, clSource, true, CL_MAP_READ, 0, source.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 2, sizeof(clSource), &clSource);

    cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
    clSetKernelArg(kernel, 3, sizeof(sampler), &sampler);

    clSetKernelArg(kernel, 4, sizeof(image.width), &image.width);

    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY|CL_MEM_USE_HOST_PTR, image.data.size*sizeof(float), (void*)image.data.data, 0);
    void* imageMap = clEnqueueMapBuffer(queue, clImage, true, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 5, sizeof(clImage), &clImage);

    size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0, 0, 0, 0) );
    clEnqueueUnmapMemObject(queue, clImage, imageMap, 0, 0, 0);
    clReleaseMemObject(clImage);
    clEnqueueUnmapMemObject(queue, clSource, volumeMap, 0, 0, 0);
    clReleaseMemObject(clSource);
    clFinish(queue);
}

#else
/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume = volume;
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        float4 start, step, end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) { row[x] = intersect(projection, vec2(x,y), volume, start, step, end) ? project(start, step, end, volume, source.data) : 0; }
    }, coreCount);
}
#endif
