#include "project.h"
#include "thread.h"
#include "opencl.h"

KERNEL(project)

struct CLProjection {
 float3 origin;
 float2 plusMinusHalfHeightMinusOriginZ; // halfHeight - origin.z
 float3 ray[3];
 float c; // origin.xy² - r²
 float radiusSq;
 float halfHeight;
 float3 dataOrigin; // origin + (volumeSize-1)/2
};

/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& volume, const Projection& projection) {
    static cl_kernel kernel = project();
    if(kernel) {
        float radius = float(volume.sampleCount.x-1)/2;
        float halfHeight = float(volume.sampleCount.z-1 -1/*FIXME*/ )/2; // Cylinder parameters (N-1 [domain size] - epsilon)
        float3 dataOrigin = {float(volume.sampleCount.x-1)/2, float(volume.sampleCount.y-1)/2, float(volume.sampleCount.z-1 -1 /*FIXME*/)/2};
        CLProjection clProjection = {projection.origin, (float2){halfHeight,-halfHeight} - projection.origin.z, {projection.ray[0], projection.ray[1], projection.ray[2]}, sq(projection.origin.xy()) - radius*radius, radius*radius, halfHeight, (float3){projection.origin.x,projection.origin.y,projection.origin.z} + dataOrigin};
        clSetKernelArg(kernel, 0, sizeof(CLProjection), &clProjection); //TODO: generic argument wrapper

        cl_image_format format = {CL_R, CL_FLOAT};
#if CL_1_2
        cl_image_desc desc = {CL_MEM_OBJECT_IMAGE3D, (size_t)volume.sampleCount.x, (size_t)volume.sampleCount.y, (size_t)volume.sampleCount.z, 0,0,0,0,0,0};
        cl_mem clSource = clCreateImage(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, &format, &desc, volume.data, 0);
#else
        static cl_mem clSource = clCreateImage3D(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, &format, volume.sampleCount.x, volume.sampleCount.y, volume.sampleCount.z, 0,0, volume.data, 0);
#endif
        static void* volumeMap = clEnqueueMapBuffer(queue, clSource, true, CL_MAP_READ, 0, volume.data.size*sizeof(float), 0, 0, 0, 0);
        clSetKernelArg(kernel, 1, sizeof(clSource), &clSource);

        cl_sampler sampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
        clSetKernelArg(kernel, 2, sizeof(sampler), &sampler);

        clSetKernelArg(kernel, 3, sizeof(image.width), &image.width);

        cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY, image.data.size*sizeof(float), 0, 0);
        //void* imageMap = clEnqueueMapBuffer(queue, clImage, true, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
        clSetKernelArg(kernel, 4, sizeof(clImage), &clImage);

        size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
        clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0, 0, 0, 0) );

        clEnqueueReadBuffer(queue, clImage, true, 0,  image.data.size*sizeof(float), (void*)image.data.data, 0,0,0);
        //clEnqueueUnmapMemObject(queue, clImage, imageMap, 0, 0, 0);
        clReleaseMemObject(clImage);
        //clEnqueueUnmapMemObject(queue, clSource, volumeMap, 0, 0, 0);
        //clReleaseMemObject(clSource);
    } else {
        const CylinderVolume source = volume;
        parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
            v4sf start, step, end;
            mref<float> row = image.data.slice(y*image.width, image.width);
            for(uint x: range(row.size)) { row[x] = intersect(projection, vec2(x,y), source, start, step, end) ? project(start, step, end, source, volume.data) : 0; }
        }, coreCount);
    }
}

