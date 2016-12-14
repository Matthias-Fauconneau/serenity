#pragma once
#include "UniformSampler.h"
#include "math/Vec.h"
#include "io/FileUtils.h"

struct PathSampleGenerator {
public:
    virtual ~PathSampleGenerator() {}

    virtual bool nextBoolean(float pTrue) = 0;
    virtual int nextDiscrete(int numChoices) = 0;
    virtual float next1D() = 0;
    virtual Vec2f next2D() = 0;

    virtual UniformSampler &uniformGenerator() = 0;
};
