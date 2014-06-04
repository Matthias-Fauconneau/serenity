#pragma once
#include "volume.h"
#include "project.h"

KERNEL(sum, SSQ) // __global float*  A, __global float* blockSums, size_t count, __local volatile float* sdata
inline float SSQ(const VolumeF& A) {
    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_ONLY, A.size.x*A.size.y*A.size.z * sizeof(float), 0, 0);
    clEnqueueCopyImageToBuffer(queue, A.data.pointer, buffer, (size_t[]){0,0,0}, (size_t[]){size_t(A.size.x),size_t(A.size.y),size_t(A.size.z)}, 0,0,0,0); // FIXME

    size_t elementCount = A.size.z*A.size.y*A.size.x;
    size_t threadCount = 128; // blockSize
    assert_(elementCount % threadCount == 0); //FIXME
    size_t blockCount = elementCount / threadCount;
    cl_mem output = clCreateBuffer(context, CL_MEM_READ_WRITE, blockCount * sizeof(float), 0, 0);
    setKernelArgs(SSQKernel, buffer/*A.data.pointer*/, output, elementCount);
    clSetKernelArg(SSQKernel, 3, threadCount*sizeof(float), 0);
    size_t localSize = threadCount;
    size_t globalSize = blockCount * localSize;
    clCheck( clEnqueueNDRangeKernel(queue, SSQKernel, 1, 0, &globalSize, &localSize, 0, 0, 0), "SSQ");
    float blockSums[blockCount];
    clEnqueueReadBuffer(queue, output, true, 0, blockCount*sizeof(float), blockSums, 0,0,0);
    clReleaseMemObject(output);
    return sum(ref<float>(blockSums,blockCount));
}

KERNEL(sum, SSE) //__global float* A, __global float* B, __global float* output, size_t count, __local volatile float* scratch
inline float SSE(const VolumeF& A, const VolumeF& B) {
    cl_mem Abuffer = clCreateBuffer(context, CL_MEM_READ_ONLY, A.size.x*A.size.y*A.size.z * sizeof(float), 0, 0);
    clEnqueueCopyImageToBuffer(queue, A.data.pointer, Abuffer, (size_t[]){0,0,0}, (size_t[]){size_t(A.size.x),size_t(A.size.y),size_t(A.size.z)}, 0,0,0,0); // FIXME
    cl_mem Bbuffer = clCreateBuffer(context, CL_MEM_READ_ONLY, B.size.x*B.size.y*B.size.z * sizeof(float), 0, 0);
    clEnqueueCopyImageToBuffer(queue, B.data.pointer, Bbuffer, (size_t[]){0,0,0}, (size_t[]){size_t(B.size.x),size_t(B.size.y),size_t(B.size.z)}, 0,0,0,0); // FIXME

    size_t elementCount = A.size.z*A.size.y*A.size.x;
    size_t threadCount = 128; //
    assert_(elementCount % threadCount == 0); //FIXME
    size_t blockCount = elementCount / threadCount;
    cl_mem output = clCreateBuffer(context, CL_MEM_READ_WRITE, blockCount * sizeof(float), 0, 0);
    setKernelArgs(SSEKernel, Abuffer, Bbuffer, output, elementCount);
    clSetKernelArg(SSEKernel, 4, threadCount*sizeof(float), 0);
    size_t localSize = threadCount;
    size_t globalSize = blockCount * localSize;
    clCheck( clEnqueueNDRangeKernel(queue, SSEKernel, 1, 0, &globalSize, &localSize, 0, 0, 0), "SSE");
    float buffer[blockCount];
    clEnqueueReadBuffer(queue, output, true, 0, blockCount*sizeof(float), buffer, 0,0,0);
    clReleaseMemObject(output);
    return sum(ref<float>(buffer,blockCount));
}

KERNEL(sum, dotProduct) //__global float* A, __global float* B, __global float* output, size_t count, __local volatile float* scratch
inline float dotProduct(const VolumeF& A, const VolumeF& B) {
    cl_mem Abuffer = clCreateBuffer(context, CL_MEM_READ_ONLY, A.size.x*A.size.y*A.size.z * sizeof(float), 0, 0);
    clEnqueueCopyImageToBuffer(queue, A.data.pointer, Abuffer, (size_t[]){0,0,0}, (size_t[]){size_t(A.size.x),size_t(A.size.y),size_t(A.size.z)}, 0,0,0,0); // FIXME
    cl_mem Bbuffer = clCreateBuffer(context, CL_MEM_READ_ONLY, B.size.x*B.size.y*B.size.z * sizeof(float), 0, 0);
    clEnqueueCopyImageToBuffer(queue, B.data.pointer, Bbuffer, (size_t[]){0,0,0}, (size_t[]){size_t(B.size.x),size_t(B.size.y),size_t(B.size.z)}, 0,0,0,0); // FIXME

    size_t elementCount = A.size.z*A.size.y*A.size.x;
    size_t threadCount = 128; //
    assert_(elementCount % threadCount == 0); //FIXME
    size_t blockCount = elementCount / threadCount;
    cl_mem output = clCreateBuffer(context, CL_MEM_READ_WRITE, blockCount * sizeof(float), 0, 0);
    setKernelArgs(dotProductKernel, Abuffer, Bbuffer, output, elementCount);
    clSetKernelArg(dotProductKernel, 4, threadCount*sizeof(float), 0);
    size_t localSize = threadCount;
    size_t globalSize = blockCount * localSize;
    clCheck( clEnqueueNDRangeKernel(queue, dotProductKernel, 1, 0, &globalSize, &localSize, 0, 0, 0), "SSE");
    float buffer[blockCount];
    clEnqueueReadBuffer(queue, output, true, 0, blockCount*sizeof(float), buffer, 0,0,0);
    clReleaseMemObject(output);
    return sum(ref<float>(buffer,blockCount));
}
