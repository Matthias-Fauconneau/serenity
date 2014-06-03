#pragma once
#include "image.h"
#include "opencl.h"

struct VolumeF {
    VolumeF() {}
    VolumeF(int3 size, const ref<float>& data={}) : size(size) {
        assert_(data.size == (size_t)size.x*size.y*size.z);
        cl_image_format format = {CL_R, CL_FLOAT};
        this->data.pointer = clCreateImage3D(context, CL_MEM_READ_WRITE | (data?CL_MEM_COPY_HOST_PTR:0), &format, size.x, size.y, size.z, 0,0, (float*)data.data, 0);
        assert_(this->data.pointer);
    }
    default_move(VolumeF);
    ~VolumeF() { if(data) clReleaseMemObject(data.pointer); data=0; size=0; }
    explicit operator bool() const { return data; }
    //size_t size() const { return (size_t)size.x*size.y*size.z; }

    int3 size = 0; // (width, height, depth)
    handle<cl_mem> data;
};

inline ImageF slice(const VolumeF& volume, size_t z) {
    int3 size = volume.size;
    ImageF image(size.xy());
    clCheck( clEnqueueReadImage(queue, volume.data.pointer, true, (size_t[]){0,0,z}, (size_t[]){size_t(size.x),size_t(size.y),1}, 0,0, (float*)image.data.data, 0,0,0) );
    return image;
}

typedef VolumeF ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)
