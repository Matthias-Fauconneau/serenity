#include "project.h"
#include "thread.h"

#define CL 0
#if CL
#include <CL/opencl.h> //OpenCL
cl_context context;
cl_command_queue queue;
cl_kernel kernel;

void __attribute((constructor(1002))) setup_cl() {
    // Creates context
    cl_platform_id platform = 0; uint platformCount;
    clGetPlatformIDs(1, &platform, &platformCount);
    cl_device_id device = 0; uint deviceCount;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_DEFAULT, 1, &device, &deviceCount);
    context = clCreateContext(0, 1, &device, 0, 0, 0);
    queue = clCreateCommandQueue(context, device, 0, 0);

    // Creates kernel
    string source = "";
    cl_program program = clCreateProgramWithSource(context, 1, &source.data, &source.size, 0);
    clBuildProgram(program, 1, &device, 0, 0, 0);
    kernel = clCreateKernel(program, "project", 0);
}
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY|CL_MEM_USE_HOST_PTR, image.data.size*sizeof(float), image.data, 0);
    void* imageMap = clEnqueueMapBuffer(queue, clImage, CL_TRUE, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&clImage);
    cl_mem clSource = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, source.data.size*sizeof(float), source.data, 0);
    void* sourceMap = clEnqueueMapBuffer(queue, clSource, CL_TRUE, CL_MAP_READ, 0, source.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), (void *)&clSource);
    clSetKernelArg(kernel, 2, sizeof(Projection), (void *)&projection);
    // Execute the OpenCL kernel on the list
    size_t globalSize[] = {(size_t)image.size().x, (size_t)image.size().y};
    size_t localSize[] = {16, 16};
    clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, localSize, 0, 0, 0);
    clEnqueueUnmapMemObject(queue, clImage, imageMap, 0, 0, 0);
    clReleaseMemObject(clImage);
    clEnqueueUnmapMemObject(queue, clSource, sourceMap, 0, 0, 0);
    clReleaseMemObject(clSource);
}

#else
/// Projects \a volume onto \a image according to \a projection
void project(const ImageF& image, const VolumeF& source, const Projection& projection) {
    const CylinderVolume volume = source;
    parallel(image.height, [&projection, &volume, &source, &image](uint, uint y) {
        v4sf start, step, end;
        mref<float> row = image.data.slice(y*image.width, image.width);
        for(uint x: range(row.size)) { row[x] = intersect(projection, vec2(x,y), volume, start, step, end) ? project(start, step, end, volume, source.data) : 0; }
    }, coreCount);
}
#endif
