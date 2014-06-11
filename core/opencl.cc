#include "opencl.h"
#include <CL/opencl.h> //OpenCL

static void clNotify(const char* info, const void *, size_t, void *) { error(info); }

string clErrors[] = {"CL_SUCCESS"_, "CL_DEVICE_NOT_FOUND"_, "CL_DEVICE_NOT_AVAILABLE"_, "CL_COMPILER_NOT_AVAILABLE"_, "CL_MEM_OBJECT_ALLOCATION_FAILURE"_, "CL_OUT_OF_RESOURCES"_, "CL_OUT_OF_HOST_MEMORY"_, "CL_PROFILING_INFO_NOT_AVAILABLE"_, "CL_MEM_COPY_OVERLAP"_, "CL_IMAGE_FORMAT_MISMATCH"_, "CL_IMAGE_FORMAT_NOT_SUPPORTED"_, "CL_BUILD_PROGRAM_FAILURE"_, "CL_MAP_FAILURE"_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, "CL_INVALID_VALUE"_, "CL_INVALID_DEVICE_TYPE"_, "CL_INVALID_PLATFORM"_, "CL_INVALID_DEVICE"_, "CL_INVALID_CONTEXT"_, "CL_INVALID_QUEUE_PROPERTIES"_, "CL_INVALID_COMMAND_QUEUE"_, "CL_INVALID_HOST_PTR"_, "CL_INVALID_MEM_OBJECT"_, "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR"_, "CL_INVALID_IMAGE_SIZE"_, "CL_INVALID_SAMPLER"_, "CL_INVALID_BINARY"_, "CL_INVALID_BUILD_OPTIONS"_, "CL_INVALID_PROGRAM"_, "CL_INVALID_PROGRAM_EXECUTABLE"_, "CL_INVALID_KERNEL_NAME"_, "CL_INVALID_KERNEL_DEFINITION"_, "CL_INVALID_KERNEL"_, "CL_INVALID_ARG_INDEX"_, "CL_INVALID_ARG_VALUE"_, "CL_INVALID_ARG_SIZE"_, "CL_INVALID_KERNEL_ARGS"_, "CL_INVALID_WORK_DIMENSION"_, "CL_INVALID_WORK_GROUP_SIZE"_, "CL_INVALID_WORK_ITEM_SIZE"_, "CL_INVALID_GLOBAL_OFFSET"_, "CL_INVALID_EVENT_WAIT_LIST"_, "CL_INVALID_EVENT"_, "CL_INVALID_OPERATION"_, "CL_INVALID_GL_OBJECT"_, "CL_INVALID_BUFFER_SIZE"_, "CL_INVALID_MIP_LEVEL"_, "CL_INVALID_GLOBAL_WORK_SIZE"_};
#define clCheck(expr, args...) ({ int _status = expr; assert_(!_status, clErrors[-_status], #expr, ##args); })

static cl_context context;
static cl_command_queue queue;
static cl_device_id device;

void __attribute((constructor(1002))) setup_cl() {
    uint platformCount; clGetPlatformIDs(0, 0, &platformCount);
    cl_platform_id platforms[platformCount]; clGetPlatformIDs(platformCount, platforms, 0);
    for(cl_platform_id platform: ref<cl_platform_id>(platforms, platformCount)) {
        for(cl_platform_info attribute : { CL_PLATFORM_NAME, CL_PLATFORM_VENDOR, CL_PLATFORM_VERSION, CL_PLATFORM_PROFILE, CL_PLATFORM_EXTENSIONS }) {
            size_t size; clGetPlatformInfo(platform, attribute, 0, 0, &size);
            char info[size]; clGetPlatformInfo(platform, attribute, size, info, 0);
            //log(string(info,size-1));
        }
    }
    if(platformCount) {
        cl_platform_id platform = platforms[0]; // {AMD, Intel, Beignet}
        uint deviceCount; clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, 0, &deviceCount);
        cl_device_id devices[deviceCount]; clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, deviceCount, devices, 0);
        for(cl_device_id device: ref<cl_device_id>(devices, deviceCount)) {
            for(cl_device_info attribute : { CL_DEVICE_NAME, CL_DEVICE_VERSION, CL_DRIVER_VERSION}) {
                size_t size; clGetDeviceInfo(device, attribute, 0, 0, &size);
                char info[size]; clGetDeviceInfo(device, attribute, size, info, 0);
                //log(string(info,size-1));
            }
        }
        device = devices[0]; // {GPU, CPU}
        context = clCreateContext(0, 1, &device, &clNotify, 0, 0);
        queue = clCreateCommandQueue(context, device, 0, 0);
    }
}

CLKernel::CLKernel(string source, string name) : name(name) {
    assert_(context);
    int status;
    cl_program program = clCreateProgramWithSource(context, 1, &source.data, &source.size, &status);
    if(clBuildProgram(program, 0, 0, 0, 0, 0)) {
        size_t buildLogSize;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, 0, &buildLogSize);
        char buildLog[buildLogSize];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, buildLogSize, (void*)buildLog, 0);
        array<string> lines = split(string(buildLog,buildLogSize-1),'\n');
        log(join(lines.slice(0,min(16ul,lines.size)),"\n"_));
        error(name);
    }
    kernel = clCreateKernel(program, strz(name), &status);
    assert_(!status && kernel, clErrors[-status]);
}

void CLKernel::setKernelArg(uint index, size_t size, const void* value) { clCheck( ::clSetKernelArg(kernel, index, size, value) ); }
void CLKernel::enqueueNDRangeKernel(cl_uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size) { clCheck( ::clEnqueueNDRangeKernel(queue, kernel, work_dim, global_work_offset, global_work_size, local_work_size, 0,0,0) ); }

CLMem::~CLMem() { if(pointer) clReleaseMemObject(pointer); pointer=0; }

CLRawBuffer::CLRawBuffer(size_t size) : CLMem(clCreateBuffer(context, CL_MEM_READ_WRITE, size, 0, 0)) {}
CLRawBuffer::CLRawBuffer(const ref<byte> data) : CLMem(clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, data.size, (byte*)data.data, 0)) {}
void CLRawBuffer::read(const mref<byte>& target) { clEnqueueReadBuffer(queue, pointer, true, 0, target.size, target, 0,0,0); }

CLVolume::CLVolume(int3 size, const ref<float>& data) : CLMem(clCreateImage3D(context, CL_MEM_READ_WRITE | (data?CL_MEM_COPY_HOST_PTR:0), (cl_image_format[]){{CL_R, CL_FLOAT}}, size.x, size.y, size.z, 0,0, (float*)data.data, 0)), size(size) {
    assert_(!data || data.size == (size_t)size.x*size.y*size.z, data.size, (size_t)size.x*size.y*size.z);
    if(!data) clEnqueueFillImage(queue, pointer, (float[]){0,0,0,0},  (size_t[]){0,0,0}, (size_t[]){size_t(size.x),size_t(size.y),size_t(size.z)}, 0,0,0);
}

void copy(const CLVolume& target, const CLVolume& source) {
    assert_(target.size == source.size);
    clCheck( clEnqueueCopyImage(queue, source, target, (size_t[]){0,0,0}, (size_t[]){0,0,0}, (size_t[]){size_t(target.size.x),size_t(target.size.y),size_t(target.size.z)}, 0,0,0) );
}

void copy(const CLBufferF& buffer, const CLVolume& volume) {
    assert_(buffer.size == (size_t)volume.size.x*volume.size.y*volume.size.z);
    clCheck( clEnqueueCopyImageToBuffer(queue, volume, buffer, (size_t[]){0,0,0}, (size_t[]){size_t(volume.size.x),size_t(volume.size.y),size_t(volume.size.z)}, 0,0,0,0) );
}

void copy(const CLVolume& volume, size_t index, const CLBufferF& slice) {
    assert_(index < (size_t)volume.size.z && (size_t)volume.size.x*volume.size.y == slice.size);
    clCheck( clEnqueueCopyBufferToImage(queue, slice, volume, 0, (size_t[]){0,0,index}, (size_t[]){size_t(volume.size.x),size_t(volume.size.y),size_t(1)}, 0,0,0) );
}

ImageF slice(const CLVolume& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    ImageF image(size.xy());
    assert_(index < size_t(size.z), index);
    clCheck( clEnqueueReadImage(queue, volume, true, (size_t[]){0,0,index}, (size_t[]){size_t(size.x),size_t(size.y),1}, 0,0, image.data, 0,0,0), "slice");
    return image;
}

cl_sampler clampToEdgeSampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP_TO_EDGE, CL_FILTER_LINEAR, 0);
cl_sampler clampSampler = clCreateSampler(context, false, CL_ADDRESS_CLAMP, CL_FILTER_LINEAR, 0);
