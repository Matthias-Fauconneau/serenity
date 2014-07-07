#pragma once
#include "thread.h"
#include "data.h"
#include "image.h"
#include "volume.h"

typedef struct _cl_kernel* cl_kernel;
typedef struct _cl_mem* cl_mem;
typedef struct _cl_sampler* cl_sampler;

/// Generic OpenCL MemObject
struct CLMem : handle<cl_mem> {
    String name;
    static uint handleCount; // Counts currently allocated MemObjects (used to assert correct release)

    CLMem(){}
    CLMem(cl_mem mem, string name) : handle(mem), name(copy(String(name))) { assert_(mem); handleCount++; }
    default_move(CLMem);
    /// Releases the OpenCL MemObject.
    ~CLMem();
};

// Raw OpenCL Buffer (used by \a CLBuffer)
struct CLRawBuffer : CLMem {
    CLRawBuffer(size_t size, string name);
    CLRawBuffer(const ref<byte> data, string name);
    default_move(CLRawBuffer);

    void read(const mref<byte>& target);
};

/// Generic typed OpenCL Buffer
generic struct CLBuffer : CLRawBuffer {
    CLBuffer(size_t size, string name) : CLRawBuffer(size*sizeof(T), name), size(size) {}
    CLBuffer(const ref<T>& data, string name) : CLRawBuffer(cast<byte>(data), name), size(data.size) {}
    default_move(CLBuffer);

    void read(const mref<T>& target) { CLRawBuffer::read(mcast<byte>(target)); }

    size_t size;
};

/// Float OpenCL Buffer
typedef CLBuffer<float> CLBufferF;

/// Single channel floating-point OpenCL 2D image
struct CLImage : CLMem {
    CLImage(int2 size, const float value, string name);
    CLImage(int2 size, const ref<float>& data, string name);
    default_move(CLImage);

    int2 size; // width, height
};

/// Single channel floating-point OpenCL 3D image
struct CLVolume : CLMem {
    CLVolume(){}
    CLVolume(int3 size, const float value, string name);
    CLVolume(int3 size, const ref<float>& data, string name);
    CLVolume(const VolumeF& A) : CLVolume(A.size, A.data, A.name) {}
    default_move(CLVolume);

    void read(const VolumeF& target, int3 origin=0) const;
    VolumeF read(VolumeF&& target, int3 origin=0) const { read(target, origin); return move(target); }

    int3 size; // (width, height, depth or index)
};

/// Copy volume into volume
const CLVolume& copy(const CLVolume& source, CLVolume& target, const int3 sourceOrigin=0, const int3 targetOrigin=0, int3 size=0);

/// Copy volume into buffer
void copy(const CLBufferF& target, const CLVolume& source, const int3 origin=0, const int3 size=0);

/// Copy buffer into volume
void copy(const CLVolume& target, const CLBufferF& source);

/// Inserts a slice buffer into volume
void copy(const CLVolume& target, size_t index, const CLBufferF& source);

/// Reads a slice back to host memory
ImageF slice(const CLVolume& source, size_t index /* Z slice or projection*/);

typedef CLVolume ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)

/// OpenCL samplers to be used as arguments to sampler_t kernel parameters
extern cl_sampler noneNearestSampler, noneLinearSampler, clampToEdgeLinearSampler, clampLinearSampler;

/// OpenCL kernel
struct CLKernel {
    handle<cl_kernel> kernel;
    Lock lock; // For atomic execution of clSetKernelArgs and clEnqueueNDRangeKernel
    size_t localSpace = 0; // Per thread group scratch space to allocate (in bytes). When non-zero, scratch space is passed as last kernel argument
    String name;

    /// Compiles kernel \a name defined by \a source
    CLKernel(string source, string name);

    void setKernelArg(uint index, size_t size, const void* value);
    void setKernelArgs(uint index) { if(localSpace) setKernelArg(index, localSpace, 0); }
    template<Type T, Type... Args> void setKernelArgs(uint index, const T& value, const Args&... args) { setKernelArg(index, sizeof(value), &value); setKernelArgs(index+1, args...); } //TODO: type check
    // Specialization for CLMem objects to pass only the pointer (and not the wrapper object).
    template<Type... Args> void setKernelArgs(uint index, const CLRawBuffer& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }
    template<Type... Args> void setKernelArgs(uint index, const CLBufferF& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }
    template<Type... Args> void setKernelArgs(uint index, const CLImage& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }
    template<Type... Args> void setKernelArgs(uint index, const CLVolume& value, const Args&... args) { setKernelArg(index, sizeof(value.pointer), &value.pointer); setKernelArgs(index+1, args...); }

    uint64 enqueueNDRangeKernel(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size);
    template<Type... Args> uint64 execute(uint work_dim, const size_t* global_work_offset, const size_t* global_work_size, const size_t* local_work_size, const Args&... args) {
        Locker lock(this->lock);
        setKernelArgs(0, args...);
        return enqueueNDRangeKernel(work_dim, global_work_offset, global_work_size, local_work_size);
    }

    /// Enqueues a command to execute a 1D kernel on a device.
    /// \param threadGroupCount The number of thread groups to schedule
    /// \param threadCount The number of local threads per groups
    /// \param args Arguments to be passed to the kernel
    template<Type... Args> uint64 operator()(size_t threadGroupCount, size_t threadCount, const Args&... args) { return execute(1, 0, (size_t[]){threadGroupCount*threadCount}, &threadCount, args...); }
    /// Enqueues a command to execute a 2D kernel on a device.
    /// \param size The total number of jobs for each dimensions
    /// \param args Arguments to be passed to the kernel
    template<Type... Args> uint64 operator()(int2 size, const Args&... args) { return execute(2, 0, (size_t[]){size_t(size.x), size_t(size.y)}, 0, args...); }
    /// Enqueues a command to execute a 3D kernel on a device.
    /// \param size The total number of jobs for each dimensions
    /// \param args Arguments to be passed to the kernel
    template<Type... Args> uint64 operator()(int3 size, const Args&... args) { return execute(3, 0, (size_t[]){size_t(size.x), size_t(size.y), size_t(size.z)}, 0, args...); }
};

/// Compiles OpenCL kernel \a name defined in \a file .cl
/// \note \a file will be linked by the build system
#define CL(file, name) \
    extern char _binary_ ## file ##_cl_start[], _binary_ ## file ##_cl_end[]; \
    static CLKernel name (ref<byte>(_binary_ ## file ##_cl_start,_binary_ ## file ##_cl_end), str(#name));

/// Executes a kernel writing to 3D image \a y by writing to a temporary buffer and copying to \a y in order to workaround NVidia's OpenCL implementation missing support for writes to 3D images.
template<Type... Args> inline uint64 emulateWriteTo3DImage(CLKernel& kernel, const CLVolume& y, const Args&... args) {
    CLBufferF buffer (y.size.z*y.size.y*y.size.x, y.name);
    uint64 time = kernel(y.size, buffer.pointer, y.size.y*y.size.x, y.size.x, args...);
    copy(y, buffer);
    return time;
}
