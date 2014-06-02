#pragma once
#include "volume.h"
#include "project.h"
#include "thread.h"

KERNEL(sum, SSQ)
// __kernel void SSQ(__global float* A, __global float* output, size_t count, __local volatile float* scratch)

inline float SSQ(const VolumeF& A) {
    static cl_kernel kernel = SSQ();
    clSetKernelArg(kernel, 0, sizeof(A.data.pointer), &A.data.pointer);
    size_t elementCount = A.size();
    size_t threadCount = 128; //
    assert_(elementCount % threadCount == 0); //FIXME
    size_t blockCount = elementCount / threadCount;
    cl_mem output = clCreateBuffer(context, CL_MEM_READ_WRITE, blockCount * sizeof(float), 0, 0);
    clSetKernelArg(kernel, 1, sizeof(output), &output);
    clSetKernelArg(kernel, 2, sizeof(elementCount), &elementCount);
    size_t localSize = threadCount;
    size_t globalSize = blockCount * localSize;
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 1, 0, &globalSize, &localSize, 0, 0, 0) );
    float buffer[blockCount];
    clEnqueueReadBuffer(queue, output, true, 0, blockCount*sizeof(float), buffer, 0,0,0);
    clReleaseMemObject(output);
    return sum(ref<float>(buffer,blockCount));
}

KERNEL(sum, SSE)
// __kernel void SSE(__global float* A, __global float* B, __global float* output, size_t count, __local volatile float* scratch)

inline float SSE(const VolumeF& A, const VolumeF& B) {
    static cl_kernel kernel = SSE();
    clSetKernelArg(kernel, 0, sizeof(A.data.pointer), &A.data.pointer);
    clSetKernelArg(kernel, 1, sizeof(B.data.pointer), &B.data.pointer);
    size_t elementCount =  A.size();
    size_t threadCount = 128; //
    assert_(elementCount % threadCount == 0); //FIXME
    size_t blockCount = elementCount / threadCount;
    cl_mem output = clCreateBuffer(context, CL_MEM_READ_WRITE, blockCount * sizeof(float), 0, 0);
    clSetKernelArg(kernel, 2, sizeof(output), &output);
    clSetKernelArg(kernel, 3, sizeof(elementCount), &elementCount);
    size_t localSize = threadCount;
    size_t globalSize = blockCount * localSize;
    clCheck( clEnqueueNDRangeKernel(queue, kernel, 1, 0, &globalSize, &localSize, 0, 0, 0) );
    float buffer[blockCount];
    clEnqueueReadBuffer(queue, output, true, 0, blockCount*sizeof(float), buffer, 0,0,0);
    clReleaseMemObject(output);
    return sum(ref<float>(buffer,blockCount));
}
