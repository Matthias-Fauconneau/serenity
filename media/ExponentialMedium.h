#pragma once
#include "Medium.h"

class ExponentialMedium : public Medium
{
    Vec3f _materialSigmaA, _materialSigmaS;
    float _density;
    float _falloffScale;
    Vec3f _unitPoint;
    Vec3f _falloffDirection;

    Vec3f _unitFalloffDirection;
    Vec3f _sigmaA, _sigmaS;
    Vec3f _sigmaT;
    bool _absorptionOnly;

    inline float density(Vec3f p) const;
    inline float density(float x, float dx, float t) const;
    inline float densityIntegral(float x, float dx, float tMax) const;
    inline float inverseOpticalDepth(float x, float dx, float sigmaT, float logXi) const;

public:
    ExponentialMedium();

    virtual void fromJson(const rapidjson::Value &v, const Scene &scene) override;

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
};
