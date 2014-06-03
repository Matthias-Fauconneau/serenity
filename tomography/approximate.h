#pragma once
#include "reconstruction.h"
//include "filter.h"

struct Approximate : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r, AtAp;
    const bool filter, regularize;

    // Projects voxel coordinates to image coordinates for bilinear sample
    struct ProjectionArray {
        struct mat4x3 { float4 rows[3]; };
        ProjectionArray(const ref<Projection>& projections, int3 volumeSize, int2 imageSize) : size(projections.size) {
            buffer<mat4x3> clProjections (projections.size);
            for(uint i: range(projections.size)) {
                const Projection& projection = projections[i];
                const float radius = float(volumeSize.x-1)/2;
                const float scale = float(imageSize.x-1)/radius;
                const mat3 transform = mat3().scale(vec3(1,scale,scale)).rotateZ( -projection.angle );
                mat4x3& p = clProjections[i];
                for(uint i: range(3)) p.rows[i] = {transform.row(i), -projection.offset[i]};
            }
            data.pointer = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, projections.size*sizeof(mat4x3), (float*)clProjections.data, 0);
            assert_(data.pointer);
        }
        int size;
        handle<cl_mem> data;
    };

    ref<Projection> projections;
    ProjectionArray projectionArray;
    const ImageArray& images;

    Approximate(int3 reconstructionSize, const ref<Projection>& projections, const ImageArray& images, bool filter = false, bool regularize = false, string label=""_);
    bool step() override;

    void backproject(const VolumeF& volume);
};
