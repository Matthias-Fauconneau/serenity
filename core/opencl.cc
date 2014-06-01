#include "opencl.h"

cl_context context;
cl_command_queue queue;
cl_device_id device;

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

cl_kernel createKernel(string source, string name) {
    if(!context) return 0;
    int status;
    cl_program program = clCreateProgramWithSource(context, 1, &source.data, &source.size, &status);
    if(clBuildProgram(program, 0, 0, 0, 0, 0)) {
        size_t buildLogSize;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, 0, &buildLogSize);
        char buildLog[buildLogSize];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, buildLogSize, (void*)buildLog, 0);
        array<string> lines = split(string(buildLog,buildLogSize-1),'\n');
        log(join(lines.slice(0,min(16ul,lines.size)),"\n"_));
        error(__FILE__);
    }
    assert_(name.last()==0);
    cl_kernel kernel = clCreateKernel(program, name.data, &status);
    clCheck(status);
    assert_(kernel);
    return kernel;
}
