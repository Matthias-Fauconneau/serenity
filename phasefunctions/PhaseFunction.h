#pragma once
#include "samplerecords/PhaseSample.h"
#include "io/JsonSerializable.h"

struct PathSampleGenerator;

struct PhaseFunction : public JsonSerializable {
    virtual Vec3f eval(const Vec3f &wi, const Vec3f &wo) const = 0;
    virtual bool sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const = 0;
    virtual float pdf(const Vec3f &wi, const Vec3f &wo) const = 0;
};
