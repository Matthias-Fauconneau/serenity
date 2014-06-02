#pragma once
#include "image.h"
#include "opencl.h"

struct VolumeF {
    VolumeF() {}
    VolumeF(int3 sampleCount, const ref<float>& data={}) : sampleCount(sampleCount) {
        assert_(data.size == size());
        cl_image_format format = {CL_R, CL_FLOAT};
        this->data.pointer = clCreateImage3D(context, CL_MEM_READ_ONLY/*_WRITE*/| (data?CL_MEM_COPY_HOST_PTR:0), &format, sampleCount.x, sampleCount.y, sampleCount.z, 0,0, (float*)data.data, 0);
        assert_(this->data.pointer);
    }
    default_move(VolumeF);
    ~VolumeF() { if(data) clReleaseMemObject(data.pointer); data=0; sampleCount=0; }
    explicit operator bool() const { return data; }
    size_t size() const { return (size_t)sampleCount.x*sampleCount.y*sampleCount.z; }

    int3 sampleCount = 0; // Sample counts (along each dimensions)
    handle<cl_mem> data;
};

inline ImageF slice(const VolumeF& volume, size_t z) {
    int3 size = volume.sampleCount;
    ImageF image(size.x, size.y);
    clCheck( clEnqueueReadImage(queue, volume.data.pointer, true, (size_t[]){0,0,z}, (size_t[]){size_t(size.x),size_t(size.y),1}, 0,0, (float*)image.data.data, 0,0,0) );
    return image;
}
