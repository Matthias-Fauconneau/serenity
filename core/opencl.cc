#include "opencl.h"

cl_context context;
cl_command_queue queue;
cl_device_id devices[2];

void __attribute((constructor(1002))) setup_cl() {
    cl_platform_id platform; uint platformCount;
    clGetPlatformIDs(1, &platform, &platformCount); assert_(platformCount == 1);
    uint deviceCount;
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU/*CL_DEVICE_TYPE_ALL*/, 2, devices, &deviceCount);
    context = clCreateContext(0, deviceCount, devices, &clNotify, 0, 0);
    queue = clCreateCommandQueue(context, devices[0], 0, 0);
}

cl_kernel createKernel(string source, string name) {
    int status;
    cl_program program = clCreateProgramWithSource(context, 1, &source.data, &source.size, &status);
    clCheck(status);
    if(clBuildProgram(program, 0, 0, 0, 0, 0)) {
        size_t buildLogSize;
        clCheck( clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, 0, 0, &buildLogSize) );
        char buildLog[buildLogSize];
        clCheck( clGetProgramBuildInfo(program, devices[0], CL_PROGRAM_BUILD_LOG, buildLogSize, (void*)buildLog, 0) );
        log(join(split(string(buildLog,buildLogSize-1),'\n').slice(0,16),"\n"_));
        error();
    }
    assert_(name.last()==0);
    cl_kernel kernel = clCreateKernel(program, name.data, &status);
    clCheck(status);
    assert_(kernel);
    return kernel;
}
