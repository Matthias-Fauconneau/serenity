#pragma once
#include "PathSampleGenerator.h"
#include "UniformSampler.h"

class UniformPathSampler : public PathSampleGenerator
{
    UniformSampler _sampler;

public:
    UniformPathSampler(uint32 seed)
    : _sampler(seed)
    {
    }
    UniformPathSampler(const UniformSampler &sampler)
    : _sampler(sampler)
    {
    }


    virtual bool nextBoolean(float pTrue) override final
    {
        return _sampler.next1D() < pTrue;
    }
    virtual int nextDiscrete(int numChoices) override final
    {
        return int(_sampler.next1D()*numChoices);
    }
    virtual float next1D() override final
    {
        return _sampler.next1D();
    }
    virtual Vec2f next2D() override final
    {
        float a = _sampler.next1D();
        float b = _sampler.next1D();
        return Vec2f(a, b);
    }

    const UniformSampler &sampler() const
    {
        return _sampler;
    }

    virtual UniformSampler &uniformGenerator() override final
    {
        return _sampler;
    }
};
