#pragma once
/// \file tiff.h Triggers dependency on tiff.cc to add support for TIFF decoding in decodeImage (weak link)
#include "core.h"

struct Image16 {
    ::buffer<uint16> buffer;
    uint16* data=0; // First pixel
    uint width=0, height=0, stride=0;
    bool own=false;

    Image16(){}
    Image16(::buffer<uint16>&& buffer, uint16* data, uint width, uint height, uint stride) :
        buffer(move(buffer)),data(data),width(width),height(height),stride(stride){}
    Image16(uint16* data, uint width, uint height, uint stride) : data(data), width(width),height(height),stride(stride){}
    Image16(uint width, uint height, uint stride=0) : width(width), height(height), stride(stride?:width){
        assert(width); assert(height);
        buffer=::buffer<uint16>(height*(stride?:width)); data=buffer;
    }

    explicit operator bool() const { return data; }
    explicit operator ref<byte>() const { assert(width==stride); return ref<byte>((byte*)data,height*stride*sizeof(uint16)); }

    uint16 operator()(uint x, uint y) const {assert(x<stride && y<height,int(x),int(y),stride,width,height); return data[y*stride+x]; }
    uint16& operator()(uint x, uint y) {assert(x<stride && y<height,int(x),int(y),stride,width,height); return data[y*stride+x]; }
};

Image16 decodeTIFF16(const ref<byte>& file);
