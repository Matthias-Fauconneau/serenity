#pragma once
#include "Medium.h"

class HomogeneousMedium : public Medium
{
    Vec3f _materialSigmaA, _materialSigmaS;
    float _density;

    Vec3f _sigmaA, _sigmaS;
    Vec3f _sigmaT;
    bool _absorptionOnly;

public:
    HomogeneousMedium();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;
    virtual rapidjson::Value toJson(Allocator &allocator) const override;

    virtual bool isHomogeneous() const override;

    virtual void prepareForRender() override;

    virtual Vec3f sigmaA(Vec3f p) const override;
    virtual Vec3f sigmaS(Vec3f p) const override;
    virtual Vec3f sigmaT(Vec3f p) const override;

    virtual bool sampleDistance(PathSampleGenerator &sampler, const Ray &ray,
            MediumState &state, MediumSample &sample) const override;
    virtual Vec3f transmittance(PathSampleGenerator &sampler, const Ray &ray) const override;
    virtual float pdf(PathSampleGenerator &sampler, const Ray &ray, bool onSurface) const override;
    virtual Vec3f transmittanceAndPdfs(PathSampleGenerator &sampler, const Ray &ray, bool startOnSurface,
            bool endOnSurface, float &pdfForward, float &pdfBackward) const override;

    Vec3f sigmaA() const { return _sigmaA; }
    Vec3f sigmaS() const { return _sigmaS; }
};
