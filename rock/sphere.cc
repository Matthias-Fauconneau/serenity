/// \file sphere.cc Computes radius for each list of voxels
#include "volume-operation.h"
#include "list.h"
#include "sample.h"

/// Computes radius for each list of voxels as the radius of the ball with the same variance
array<real> varianceRadius(const buffer<array<short3>>& lists) {
    array<real> radiusList (lists.size);
    for(ref<short3> list: lists) {
        if(!list) continue;
        real3 centroid = 0;
        for(short3 position: list) centroid += real3(position);
        centroid /= list.size;
        real variance = 0;
        for(short3 position: list) variance += sq(real3(position)-centroid);
        variance /= list.size;
        radiusList << sqrt(5./3*variance);
    }
    return move(radiusList);
}

/// Computes equivalent variance radius for each list of voxels
struct VarianceRadius : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<real> radiusList = ::varianceRadius(parseLists(inputs[0]->data));
        outputs[0]->metadata = String("r(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(radiusList));
    }
};
template struct Interface<Operation>::Factory<VarianceRadius>;


/// Computes an equivalent ball radius from volume
array<real> volumeRadius(const buffer<real>& volume) {
    return apply(volume, [](real V){ return cbrt(3/(4*PI) * V); }); // V = 4π/3 r³ <=> r = (3/4π V) ¹/³
}

/// Computes equivalent volume radius for each list of voxels
struct VolumeRadius : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<real> radiusList = ::volumeRadius(parseUniformSample(inputs[0]->data));
        outputs[0]->metadata = String("r(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(radiusList));
    }
};
template struct Interface<Operation>::Factory<VolumeRadius>;

/// Computes maximum radius for each list of voxels
array<real> maximumRadius(const VolumeT<uint16>& maximum, const buffer<array<short3>>& lists) {
    array<real> radiusList (lists.size);
    for(ref<short3> list: lists) {
        if(!list) continue;
        radiusList << sqrt((real) max(apply(list, [&](short3 p){ return maximum(p.x, p.y, p.z); })));
    }
    return move(radiusList);
}

/// Computes maximum radius for each list of voxels
struct MaximumRadius : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        Volume source = toVolume(*inputs[0]);
        array<real> radiusList = maximumRadius(source, parseLists(inputs[1]->data));
        outputs[0]->metadata = String("r(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(radiusList));
    }
};
template struct Interface<Operation>::Factory<MaximumRadius>;
