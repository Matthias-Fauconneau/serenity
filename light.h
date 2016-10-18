#pragma once

#include "matrix.h"

mat4 shearedPerspective(const float s, const float t) { // Sheared perspective (rectification)
    const float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
    const float left = (-1-S), right = (1-S);
    const float bottom = (-1-T), top = (1-T);
    mat4 M;
    M(0,0) = 2 / (right-left);
    M(1,1) = 2 / (top-bottom);
    M(0,2) = (right+left) / (right-left);
    M(1,2) = (top+bottom) / (top-bottom);
    const float near = 1-1./2, far = 1+1./2;
    M(2,2) = - (far+near) / (far-near);
    M(2,3) = - 2*far*near / (far-near);
    M(3,2) = - 1;
    M(3,3) = 0;
    M.translate(vec3(-S,-T,0));
    M.translate(vec3(0,0,-1)); // 0 -> -1 (Z-)
    return M;
}

#include "file.h"
#include "data.h"

struct LightFieldFile {
    const Folder& folder = currentWorkingDirectory();
    String name;
    uint2 imageCount;
    uint2 imageSize;
    LightFieldFile() {
        for(string name: folder.list(Files)) {
            TextData s (name);
            int imageCountX = s.integer(false);
            if(!s.match('x')) continue;
            int imageCountY = s.integer(false);
            if(!s.match('x')) continue;
            int imageSizeX = s.integer(false);
            if(!s.match('x')) continue;
            int imageSizeY = s.integer(false);
            assert_(!s && imageCountX && imageCountY && imageSizeX && imageSizeY);
            this->name = copyRef(name);
            this->imageCount = uint2(imageCountX, imageCountY);
            this->imageSize = uint2(imageSizeX, imageSizeY);
            return;
        }
        error("");
    }
};

struct LightField : LightFieldFile {
    Map map {name, folder};
    ref<half> field = cast<half>(map);

    const int size1 = imageSize.x *1;
    const int size2 = imageSize.y *size1;
    const int size3 = imageCount.x*size2;
    const size_t size4 = (size_t)imageCount.y*size3;
    const struct Image4DH : ref<half> {
        uint4 size;
        Image4DH(uint2 imageCount, uint2 imageSize, ref<half> data) : ref<half>(data), size(imageCount.y, imageCount.x, imageSize.y, imageSize.x) {}
        const half& operator ()(uint s, uint t, uint u, uint v) const {
            assert_(t < size[0] && s < size[1] && v < size[2] && u < size[3], (int)s, (int)t, (int)u, (int)v);
            size_t index = (((uint64)t*size[1]+s)*size[2]+v)*size[3]+u;
            assert_(index < ref<half>::size, int(index), ref<half>::size, (int)s, (int)t, (int)u, (int)v, size);
            return operator[](index);
        }
    } fieldZ {imageCount, imageSize, field.slice(0*size4, size4)},
      fieldB {imageCount, imageSize, field.slice(1*size4, size4)},
      fieldG {imageCount, imageSize, field.slice(2*size4, size4)},
      fieldR {imageCount, imageSize, field.slice(3*size4, size4)};
};
