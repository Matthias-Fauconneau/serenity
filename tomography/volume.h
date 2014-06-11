#pragma once
#include "image.h"
#include "file.h"
#include "opencl.h"

struct VolumeF {
    VolumeF(int3 size, const ref<float>& data) : size(size), data(data) { assert_(data.size == (size_t)size.x*size.y*size.z); }
    VolumeF(const ref<float>& data) : VolumeF(round(pow(data.size,1./3)), data) {}
    VolumeF(Map&& map) : VolumeF((ref<float>)map) { this->map = move(map); }
    inline float& operator()(uint x, uint y, uint z) const {assert_(x<size.x && y<size.y && z<size.z, x,y,z, size); return data[(size_t)z*size.y*size.x+y*size.x+x]; }

    uint3 size = 0;
    buffer<float> data;
    Map map;
};

inline ImageF slice(const VolumeF& volume, size_t index /* Z slice or projection*/) {
    uint3 size = volume.size;
    assert_(index < size_t(size.z), index);
    return ImageF(buffer<float>(volume.data.slice(index*size.y*size.x,size.y*size.x)), int2(size.x,size.y));
}

struct CLVolume {
    CLVolume(int3 size, const ref<float>& data={}) : size(size) {
        assert_(!data || data.size == (size_t)size.x*size.y*size.z, data.size, (size_t)size.x*size.y*size.z);
        cl_image_format format = {CL_R, CL_FLOAT};
        this->data.pointer = clCreateImage3D(context, CL_MEM_READ_WRITE | (data?CL_MEM_COPY_HOST_PTR:0), &format, size.x, size.y, size.z, 0,0, (float*)data.data, 0);
        if(!data) clEnqueueFillImage(queue, this->data.pointer, (float[]){0,0,0,0},  (size_t[]){0,0,0}, (size_t[]){size_t(size.x),size_t(size.y),size_t(size.z)}, 0,0,0);
        assert_(this->data.pointer);
    }
    CLVolume(const ref<float>& data) : CLVolume(round(pow(data.size,1./3)), data) {}
    default_move(CLVolume);
    ~CLVolume() { if(data) clReleaseMemObject(data.pointer); data=0; size=0; }
    explicit operator bool() const { return data; }

    int3 size = 0; // (width, height, depth/index)
    handle<cl_mem> data;
};

inline ImageF slice(const CLVolume& volume, size_t index /* Z slice or projection*/) {
    int3 size = volume.size;
    ImageF image(size.xy());
    assert_(index < size_t(size.z), index);
    clCheck( clEnqueueReadImage(queue, volume.data.pointer, true, (size_t[]){0,0,index}, (size_t[]){size_t(size.x),size_t(size.y),1}, 0,0, (float*)image.data.data, 0,0,0), "slice");
    return image;
}

typedef CLVolume ImageArray; // NVidia does not implement OpenCL 1.2 (2D image arrays)
