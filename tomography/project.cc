#include "project.h"
#include "thread.h"

#define CL 1
#if CL
#include <CL/opencl.h> //OpenCL
static void clNotify(const char* info, const void *, size_t, void *) { error(info); }
static string clErrors[] = {"CL_SUCCESS"_, "CL_DEVICE_NOT_FOUND"_, "CL_DEVICE_NOT_AVAILABLE"_, "CL_COMPILER_NOT_AVAILABLE"_, "CL_MEM_OBJECT_ALLOCATION_FAILURE"_, "CL_OUT_OF_RESOURCES"_, "CL_OUT_OF_HOST_MEMORY"_, "CL_PROFILING_INFO_NOT_AVAILABLE"_, "CL_MEM_COPY_OVERLAP"_, "CL_IMAGE_FORMAT_MISMATCH"_, "CL_IMAGE_FORMAT_NOT_SUPPORTED"_, "CL_BUILD_PROGRAM_FAILURE"_, "CL_MAP_FAILURE"_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, "CL_INVALID_VALUE"_, "CL_INVALID_DEVICE_TYPE"_, "CL_INVALID_PLATFORM"_, "CL_INVALID_DEVICE"_, "CL_INVALID_CONTEXT"_, "CL_INVALID_QUEUE_PROPERTIES"_, "CL_INVALID_COMMAND_QUEUE"_, "CL_INVALID_HOST_PTR"_, "CL_INVALID_MEM_OBJECT"_, "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR"_, "CL_INVALID_IMAGE_SIZE"_, "CL_INVALID_SAMPLER"_, "CL_INVALID_BINARY"_, "CL_INVALID_BUILD_OPTIONS"_, "CL_INVALID_PROGRAM"_, "CL_INVALID_PROGRAM_EXECUTABLE"_, "CL_INVALID_KERNEL_NAME"_, "CL_INVALID_KERNEL_DEFINITION"_, "CL_INVALID_KERNEL"_, "CL_INVALID_ARG_INDEX"_, "CL_INVALID_ARG_VALUE"_, "CL_INVALID_ARG_SIZE"_, "CL_INVALID_KERNEL_ARGS"_, "CL_INVALID_WORK_DIMENSION"_, "CL_INVALID_WORK_GROUP_SIZE"_, "CL_INVALID_WORK_ITEM_SIZE"_, "CL_INVALID_GLOBAL_OFFSET"_, "CL_INVALID_EVENT_WAIT_LIST"_, "CL_INVALID_EVENT"_, "CL_INVALID_OPERATION"_, "CL_INVALID_GL_OBJECT"_, "CL_INVALID_BUFFER_SIZE"_, "CL_INVALID_MIP_LEVEL"_, "CL_INVALID_GLOBAL_WORK_SIZE"_};
void clCheck(int status) { assert_(!status, clErrors[-status]); }

static cl_context context;
static cl_command_queue queue;
static cl_kernel kernel;
#define KERNEL(name) static ref<byte> name() { \
    extern char _binary_ ## name ##_cl_start[], _binary_ ## name ##_cl_end[]; \
    return ref<byte>(_binary_ ## name ##_cl_start,_binary_ ## name ##_cl_end); \
}

KERNEL(project)
void __attribute((constructor(1002))) setup_cl() {
    // Creates context
    cl_platform_id platform; uint platformCount;
    clGetPlatformIDs(1, &platform, &platformCount); assert_(platformCount == 1);
    cl_device_id devices[2]; uint deviceCount;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU/*CL_DEVICE_TYPE_ALL*/, 2, devices, &deviceCount);
    context = clCreateContext(0, deviceCount, devices, &clNotify, 0, 0);
    queue = clCreateCommandQueue(context, devices[0], 0, 0);

    string source = project();
    cl_program program = clCreateProgramWithSource(context, 1, &source.data, &source.size, 0);
    clBuildProgram(program, 0, 0, 0, 0, 0);
    kernel = clCreateKernel(program, "project", 0);
    assert_(kernel);
}
void project(const ImageF& image, const VolumeF& source, const Projection& /*projection*/) {
    cl_mem clImage = clCreateBuffer(context, CL_MEM_WRITE_ONLY|CL_MEM_USE_HOST_PTR, image.data.size*sizeof(float), (void*)image.data.data, 0);
    void* imageMap = clEnqueueMapBuffer(queue, clImage, CL_TRUE, CL_MAP_WRITE, 0, image.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &clImage);
    cl_mem clSource = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_USE_HOST_PTR, source.data.size*sizeof(float), source.data, 0);
    void* sourceMap = clEnqueueMapBuffer(queue, clSource, CL_TRUE, CL_MAP_READ, 0, source.data.size*sizeof(float), 0, 0, 0, 0);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &clSource);
    clSetKernelArg(kernel, 2, sizeof(uint), &image.width);
    //clSetKernelArg(kernel, 2, sizeof(Projection), (void *)&projection);
    size_t globalSize[] = {(size_t)image.width, (size_t)image.height};
    //size_t localSize[] = {16, 16};
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 2, 0, globalSize, 0/*localSize*/, 0, 0, 0) );
    clEnqueueUnmapMemObject(queue, clImage, imageMap, 0, 0, 0);
    clReleaseMemObject(clImage);
    clEnqueueUnmapMemObject(queue, clSource, sourceMap, 0, 0, 0);
    clReleaseMemObject(clSource);
    clFinish(queue);
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
