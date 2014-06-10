#pragma once
#include "image.h"
#include "opencl.h"

struct VolumeF {
    VolumeF(int3 size, const ref<float>& data={}) : size(size) {
        assert_(!data || data.size == (size_t)size.x*size.y*size.z, data.size, (size_t)size.x*size.y*size.z);
        cl_image_format format = {CL_R, CL_FLOAT};
        this->data.pointer = clCreateImage3D(context, CL_MEM_READ_WRITE | (data?CL_MEM_COPY_HOST_PTR:0), &format, size.x, size.y, size.z, 0,0, (float*)data.data, 0);
        if(!data) clEnqueueFillImage(queue, this->data.pointer, (float[]){0,0,0,0},  (size_t[]){0,0,0}, (size_t[]){size_t(size.x),size_t(size.y),size_t(size.z)}, 0,0,0);
        assert_(this->data.pointer);
    }
    VolumeF(const ref<float>& data) : VolumeF(round(pow(data.size,1./3)), data) {}
    default_move(VolumeF);
    ~VolumeF() { if(data) clReleaseMemObject(data.pointer); data=0; size=0; }
    explicit operator bool() const { return data; }

    int3 size = 0; // (width, height, depth/index)
    handle<cl_mem> data;
};

inline ImageF slice(const VolumeF& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    ImageF image(size.xy());
    assert_(index < size_t(size.z), index);
    clCheck( clEnqueueReadImage(queue, volume.data.pointer, true, (size_t[]){0,0,index}, (size_t[]){size_t(size.x),size_t(size.y),1}, 0,0, (float*)image.data.data, 0,0,0), "slice");
    return image;
}

typedef VolumeF ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)
