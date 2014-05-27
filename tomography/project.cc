#include "project.h"
#include "thread.h"

#define CL 1
#if CL
#include "opencl.h"

KERNEL(project)
static cl_kernel kernel = project();

struct CLProjection { float4 origin, ray[3]; };
struct CLVolume { float4 capZ, radiusR0R0, radiusSqHeight, dataOrigin; };

void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY|CL_MEM_USE_HOST_PTR, image.data.size*sizeof(float), (void*)image.data.data, 0);
    void* imageMap = clEnqueueMapBuffer(queue, clImage, CL_TRUE, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    cl_mem clSource = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, source.data.size*sizeof(float), source.data, 0);
    void* volumeMap = clEnqueueMapBuffer(queue, clSource, CL_TRUE, CL_MAP_READ, 0, source.data.size*sizeof(float), 0, 0, 0, 0);

    clSetKernelArg(kernel, 0, sizeof(uint), &image.width);
    CLProjection clProjection = {projection.origin, {projection.ray[0],projection.ray[1],projection.ray[2]}};
    clSetKernelArg(kernel, 1, sizeof(CLProjection), &clProjection);
    CylinderVolume cylinder = source;
    CLVolume clVolume = {cylinder.capZ, cylinder.radiusR0R0, cylinder.radiusSqHeight, cylinder.dataOrigin};
    clSetKernelArg(kernel, 2, sizeof(CLVolume), &clVolume);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &clImage);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &clSource);
    //clSetKernelArg(kernel, 2, sizeof(Projection), (void *)&projection);
    size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
    //size_t localSize[] = {16, 16};
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0/*localSize*/, 0, 0, 0) );
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
