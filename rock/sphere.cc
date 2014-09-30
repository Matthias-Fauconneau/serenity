/// \file sphere.cc Computes radius for each list of voxels
#include "operation.h"
#include "list.h"
#include "sample.h"

/// Computes radius for each list of voxels
array<real> radiusList(const buffer<array<short3>>& lists) {
    array<real> radiusList (lists.size);
    for(ref<short3> list: lists) {
        if(!list) continue;
        real3 centroid = 0;
        for(short3 position: list) centroid += real3(position);
        centroid /= list.size;
        real radius = 0;
        for(short3 position: list) radius += norm(real3(position)-centroid);
        radius /= list.size;
        radiusList << radius;
    }
    return move(radiusList);
}

/// Computes radius for each list of voxels
struct RadiusList : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<real> radiusList = ::radiusList(parseLists(inputs[0]->data));
        outputs[0]->metadata = String("r(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(radiusList));
    }
};
template struct Interface<Operation>::Factory<RadiusList>;


/// Computes an equivalent ball radius from volume
array<real> equivalentRadius(const buffer<real>& volume) {
    return apply(volume, [](real V){ return cbrt(3/(4*PI) * V); }); // V = 4π/3 r³ <=> r = (3/4π V) ¹/³
}

/// Computes radius for each list of voxels
struct EquivalentRadius : Operation {
    virtual void execute(const Dict&, const ref<Result*>& outputs, const ref<const Result*>& inputs) override {
        array<real> radiusList = ::equivalentRadius(parseUniformSample(inputs[0]->data));
        outputs[0]->metadata = String("r(index).tsv"_);
        outputs[0]->data = toASCII(UniformSample(radiusList));
    }
};
template struct Interface<Operation>::Factory<EquivalentRadius>;
