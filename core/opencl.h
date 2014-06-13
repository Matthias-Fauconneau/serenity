#pragma once
#include "thread.h"
#include "string.h"
#include "vector.h"
#include "data.h"
#include "image.h"
#include "volume.h"

typedef struct _cl_kernel* cl_kernel;
typedef struct _cl_mem* cl_mem;
typedef struct _cl_sampler* cl_sampler;


struct CLMem : handle<cl_mem> {
    CLMem(cl_mem mem) : handle(mem) { assert_(mem); }
    default_move(CLMem);
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
    CLVolume(int3 size, float value=0);
    CLVolume(int3 size, const ref<float>& data);
    CLVolume(const VolumeF& A) : CLVolume(A.size, A.data) {}
    //CLVolume(const ref<float>& data) : CLVolume(round(pow(data.size,1./3)), data) {} Implemented through VolumeF
    default_move(CLVolume);

    void read(const VolumeF& target);

    int3 size; // (width, height, depth/index)
};

// Copy volume into volume
void copy(const CLVolume& target, const CLVolume& source, const int3 origin=0);

// Copy volume into buffer
void copy(const CLBufferF& target, const CLVolume& source, const int3 origin=0, const int3 size=0);

// Copy buffer into volume
void copy(const CLVolume& target, const CLBufferF& source);

// Inserts a slice buffer into volume
void copy(const CLVolume& target, size_t index, const CLBufferF& source);

ImageF slice(const CLVolume& source, size_t index /* Z slice or projection*/);

typedef CLVolume ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)

extern cl_sampler noneNearestSampler, noneLinearSampler, clampToEdgeLinearSampler, clampLinearSampler;

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

    uint64 enqueueNDRangeKernel(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size);
    template<Type... Args> uint64 execute(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size, const Args&... args) {
        Locker lock(this->lock);
        setKernelArgs(0, args...);
        return enqueueNDRangeKernel(work_dim, global_work_offset, global_work_size, local_work_size);
    }

    template<Type... Args> uint64 operator()(size_t blockCount, size_t blockSize /*threadCount*/, const Args&... args) { return execute(1, 0, (size_t[]){blockCount*blockSize}, &blockSize, args...); }
    template<Type... Args> uint64 operator()(int2 size, const Args&... args) { return execute(2, 0, (size_t[]){size_t(size.x), size_t(size.y)}, 0, args...); }
    template<Type... Args> uint64 operator()(int3 size, const Args&... args) { return execute(3, 0, (size_t[]){size_t(size.x), size_t(size.y), size_t(size.z)}, 0, args...); }
};

#define CL(file, name) \
    extern char _binary_ ## file ##_cl_start[], _binary_ ## file ##_cl_end[]; \
    namespace CL { static CLKernel name (ref<byte>(_binary_ ## file ##_cl_start,_binary_ ## file ##_cl_end), str(#name)); }

template<Type... Args> inline uint64 emulateWriteTo3DImage(CLKernel& kernel, const CLVolume& y, const Args&... args) {
    CLBufferF buffer (y.size.z*y.size.y*y.size.x);
    uint64 time = kernel(y.size, buffer.pointer, y.size.y*y.size.x, y.size.x, args...);
    copy(y, buffer); // FIXME: Nvidia OpenCL doesn't support writes to 3D images
    return time;
}
