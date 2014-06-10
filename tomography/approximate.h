#pragma once
#include "reconstruction.h"
//include "filter.h"

struct Approximate : Reconstruction  {
    real residualEnergy = 0;
    VolumeF p, r, AtAp;
    ImageArray w;
    const bool filter, regularize;

    // Projects voxel coordinates to image coordinates for bilinear sample
    struct ProjectionArray {
        ProjectionArray(const ref<Projection>& projections) : size(projections.size) {
            buffer<mat4> worldToViews (projections.size);
            for(uint i: range(projections.size)) worldToViews[i] = projections[i].worldToView;
            data.pointer = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, worldToViews.size*sizeof(mat4), (float*)worldToViews.data, 0);
            assert_(data.pointer);
        }
        int size;
        handle<cl_mem> data;
    };

    ref<Projection> projections;
    ProjectionArray projectionArray;
    const ImageArray& images;

    Approximate(int3 volumeSize, const ref<Projection>& projections, const ImageArray& images, bool filter = false, bool regularize = false, string label=""_);
    bool step() override;

    void backproject(const ImageArray& images, const VolumeF& volume);
    void project(const ImageArray& images, const VolumeF& volume);
};
