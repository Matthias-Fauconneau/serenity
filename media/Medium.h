#pragma once
#include "samplerecords/MediumSample.h"
#include "phasefunctions/PhaseFunction.h"
#include "math/Ray.h"
#include "io/JsonSerializable.h"
#include <memory>

struct Scene;

class Medium : public JsonSerializable
{
protected:
    std::shared_ptr<PhaseFunction> _phaseFunction;
    int _maxBounce;

public:
    struct MediumState
    {
        bool firstScatter;
        int component;
        int bounce;

        void reset()
        {
            firstScatter = true;
            bounce = 0;
        }

        void advance()
        {
            firstScatter = false;
            bounce++;
        }
    };

    Medium();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

    virtual bool isHomogeneous() const = 0;

    virtual void prepareForRender() {}
    virtual void teardownAfterRender() {}

    virtual Vec3f sigmaA(Vec3f p) const = 0;
    virtual Vec3f sigmaS(Vec3f p) const = 0;
    virtual Vec3f sigmaT(Vec3f p) const = 0;

    virtual bool sampleDistance(PathSampleGenerator &sampler, const Ray &ray,
            MediumState &state, MediumSample &sample) const = 0;
    virtual Vec3f transmittance(PathSampleGenerator &sampler, const Ray &ray) const = 0;
    virtual float pdf(PathSampleGenerator &sampler, const Ray &ray, bool onSurface) const = 0;
    virtual Vec3f transmittanceAndPdfs(PathSampleGenerator &sampler, const Ray &ray, bool startOnSurface,
            bool endOnSurface, float &pdfForward, float &pdfBackward) const;
    virtual const PhaseFunction *phaseFunction(const Vec3f &p) const;
};
