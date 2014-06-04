#pragma once
#include <CL/opencl.h> //OpenCL
#include "string.h"
#include "vector.h"

inline void clNotify(const char* info, const void *, size_t, void *) { error(info); }

static string clErrors[] = {"CL_SUCCESS"_, "CL_DEVICE_NOT_FOUND"_, "CL_DEVICE_NOT_AVAILABLE"_, "CL_COMPILER_NOT_AVAILABLE"_, "CL_MEM_OBJECT_ALLOCATION_FAILURE"_, "CL_OUT_OF_RESOURCES"_, "CL_OUT_OF_HOST_MEMORY"_, "CL_PROFILING_INFO_NOT_AVAILABLE"_, "CL_MEM_COPY_OVERLAP"_, "CL_IMAGE_FORMAT_MISMATCH"_, "CL_IMAGE_FORMAT_NOT_SUPPORTED"_, "CL_BUILD_PROGRAM_FAILURE"_, "CL_MAP_FAILURE"_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, ""_, "CL_INVALID_VALUE"_, "CL_INVALID_DEVICE_TYPE"_, "CL_INVALID_PLATFORM"_, "CL_INVALID_DEVICE"_, "CL_INVALID_CONTEXT"_, "CL_INVALID_QUEUE_PROPERTIES"_, "CL_INVALID_COMMAND_QUEUE"_, "CL_INVALID_HOST_PTR"_, "CL_INVALID_MEM_OBJECT"_, "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR"_, "CL_INVALID_IMAGE_SIZE"_, "CL_INVALID_SAMPLER"_, "CL_INVALID_BINARY"_, "CL_INVALID_BUILD_OPTIONS"_, "CL_INVALID_PROGRAM"_, "CL_INVALID_PROGRAM_EXECUTABLE"_, "CL_INVALID_KERNEL_NAME"_, "CL_INVALID_KERNEL_DEFINITION"_, "CL_INVALID_KERNEL"_, "CL_INVALID_ARG_INDEX"_, "CL_INVALID_ARG_VALUE"_, "CL_INVALID_ARG_SIZE"_, "CL_INVALID_KERNEL_ARGS"_, "CL_INVALID_WORK_DIMENSION"_, "CL_INVALID_WORK_GROUP_SIZE"_, "CL_INVALID_WORK_ITEM_SIZE"_, "CL_INVALID_GLOBAL_OFFSET"_, "CL_INVALID_EVENT_WAIT_LIST"_, "CL_INVALID_EVENT"_, "CL_INVALID_OPERATION"_, "CL_INVALID_GL_OBJECT"_, "CL_INVALID_BUFFER_SIZE"_, "CL_INVALID_MIP_LEVEL"_, "CL_INVALID_GLOBAL_WORK_SIZE"_};
#define clCheck(expr, args...) ({ int _status = expr; assert_(!_status, clErrors[-_status], #expr, ##args); })

extern cl_context context;
extern cl_command_queue queue;

cl_kernel createKernel(string source, string name);

#define KERNEL(file, name) \
    extern char _binary_ ## file ##_cl_start[], _binary_ ## file ##_cl_end[]; \
    static cl_kernel name ## Kernel = createKernel(ref<byte>(_binary_ ## file ##_cl_start,_binary_ ## file ##_cl_end), #name ##_);

inline void _setKernelArgs(cl_kernel, int) {}
template<Type T, Type... Args> inline void _setKernelArgs(cl_kernel k, int i, const T& t, const Args&... args) { clCheck( clSetKernelArg(k , i, sizeof(t), &t), i); _setKernelArgs(k, i+1, args...); }
template<Type... Args> inline void setKernelArgs(cl_kernel k, const Args&... args){ _setKernelArgs(k, 0, args...); }
