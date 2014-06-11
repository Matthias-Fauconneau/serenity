#pragma once
#include "thread.h"
#include "string.h"
#include "vector.h"
#include "data.h"
#include "image.h"

typedef struct _cl_kernel* cl_kernel;
typedef struct _cl_mem* cl_mem;
typedef struct _cl_sampler* cl_sampler;


struct CLMem : handle<cl_mem> {
    CLMem(cl_mem mem) : handle(mem) { assert_(mem); }
    ~CLMem();
};

struct CLRawBuffer : CLMem {
    CLRawBuffer(size_t size);
    CLRawBuffer(const ref<byte> data);
    default_move(CLRawBuffer);

    void read(const mref<byte>& target);
};

generic struct CLBuffer : CLRawBuffer {
    CLBuffer(size_t size) : CLRawBuffer(size*sizeof(T)), size(size) {}
    CLBuffer(const ref<T>& data) : CLRawBuffer(cast<byte>(data)), size(data.size) {}
    default_move(CLBuffer);

    void read(const mref<T>& target) { CLRawBuffer::read(mcast<byte>(target)); }
    size_t size;
};

typedef CLBuffer<float> CLBufferF;

struct CLVolume : CLMem {
    CLVolume(int3 size, const ref<float>& data={});
    CLVolume(const ref<float>& data) : CLVolume(round(pow(data.size,1./3)), data) {}
    default_move(CLVolume);

    int3 size; // (width, height, depth/index)
};

// Copy volume into volume
void copy(const CLVolume& buffer, const CLVolume& volume);

// Copy volume into buffer
void copy(const CLBufferF& buffer, const CLVolume& volume);

// Inserts a slice buffer into volume
void copy(const CLVolume& volume, size_t index, const CLBufferF& slice);

ImageF slice(const CLVolume& volume, size_t index /* Z slice or projection*/);

typedef CLVolume ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)

extern cl_sampler clampToEdgeSampler, clampSampler;

struct CLKernel {
    handle<cl_kernel> kernel;
    Lock lock;
    size_t localSpace = 0;
    String name;

    CLKernel(string source, string name);

    //TODO: type check
    void setKernelArg(uint index, size_t size, const void* value);
    void setKernelArgs(uint index) { if(localSpace) setKernelArg(index, localSpace, 0); }
    template<Type T, Type... Args> void setKernelArgs(uint index, const T& value, const Args&... args) { setKernelArg(index, sizeof(value), &value); setKernelArgs(index+1, args...); }
    template<Type... Args> void setKernelArgs(uint index, const CLRawBuffer& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }
    template<Type... Args> void setKernelArgs(uint index, const CLVolume& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }

    void enqueueNDRangeKernel(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size);
    template<Type... Args> void execute(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size, const Args&... args) {
        Locker lock(this->lock);
        setKernelArgs(0, args...);
        enqueueNDRangeKernel(work_dim, global_work_offset, global_work_size, local_work_size);
    }

    template<Type... Args> void operator()(size_t blockCount, size_t blockSize /*threadCount*/, const Args&... args) { execute(1, 0, (size_t[]){blockCount*blockSize}, &blockSize, args...); }
    template<Type... Args> void operator()(int2 size, const Args&... args) { execute(2, 0, (size_t[]){size_t(size.x), size_t(size.y)}, 0, args...); }
    template<Type... Args> void operator()(int3 size, const Args&... args) { execute(3, 0, (size_t[]){size_t(size.x), size_t(size.y), size_t(size.z)}, 0, args...); }
};

#define CL(file, name) \
    extern char _binary_ ## file ##_cl_start[], _binary_ ## file ##_cl_end[]; \
    namespace CL { static CLKernel name (ref<byte>(_binary_ ## file ##_cl_start,_binary_ ## file ##_cl_end), str(#name)); }
