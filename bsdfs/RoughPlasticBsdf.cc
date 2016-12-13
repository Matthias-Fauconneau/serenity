#include "RoughPlasticBsdf.h"
#include "RoughDielectricBsdf.h"
#include "Fresnel.h"
#include "samplerecords/SurfaceScatterEvent.h"
#include "materials/ConstantTexture.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/SampleWarp.h"
#include "math/MathUtil.h"
#include "math/Angle.h"
#include "math/Vec.h"
#include "io/JsonObject.h"
#include "io/Scene.h"

RoughPlasticBsdf::RoughPlasticBsdf()
: _ior(1.5f),
  _thickness(1.0f),
  _sigmaA(0.0f),
  _distributionName("ggx"),
  _roughness(std::make_shared<ConstantTexture>(0.02f))
{
    _lobes = BsdfLobes(BsdfLobes::GlossyReflectionLobe | BsdfLobes::DiffuseReflectionLobe);
}

void RoughPlasticBsdf::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Bsdf::fromJson(v, scene);
    ::fromJson(v, "ior", _ior);
    ::fromJson(v, "distribution", _distributionName);
    ::fromJson(v, "thickness", _thickness);
    ::fromJson(v, "sigma_a", _sigmaA);
    scene.textureFromJsonMember(v, "roughness", TexelConversion::REQUEST_AVERAGE, _roughness);

    // Fail early in case of invalid distribution name
    prepareForRender();
}

rapidjson::Value RoughPlasticBsdf::toJson(Allocator &allocator) const
{
    return JsonObject{Bsdf::toJson(allocator), allocator,
        "type", "rough_plastic",
        "ior", _ior,
        "thickness", _thickness,
        "sigma_a", _sigmaA,
        "distribution", _distributionName,
        "roughness", *_roughness
    };
}

bool RoughPlasticBsdf::sample(SurfaceScatterEvent &event) const
{
    if (event.wi.z() <= 0.0f)
        return false;

    bool sampleR = event.requestedLobe.test(BsdfLobes::GlossyReflectionLobe);
    bool sampleT = event.requestedLobe.test(BsdfLobes::DiffuseReflectionLobe);

    if (!sampleR && !sampleT)
        return false;

    const Vec3f &wi = event.wi;
    float eta = 1.0f/_ior;
    float Fi = Fresnel::dielectricReflectance(eta, wi.z());
    float substrateWeight = _avgTransmittance*(1.0f - Fi);
    float specularWeight = Fi;
    float specularProbability = specularWeight/(specularWeight + substrateWeight);

    if (sampleR && (event.sampler->nextBoolean(specularProbability) || !sampleT)) {
        float roughness = (*_roughness)[*event.info].x();
        if (!RoughDielectricBsdf::sampleBase(event, true, false, roughness, _ior, _distribution))
            return false;
        if (sampleT) {
            Vec3f diffuseAlbedo = albedo(event.info);
            float Fo = Fresnel::dielectricReflectance(eta, event.wo.z());

            Vec3f brdfSubstrate = ((1.0f - Fi)*(1.0f - Fo)*eta*eta)*(diffuseAlbedo/(1.0f - diffuseAlbedo*_diffuseFresnel))*INV_PI*event.wo.z();
            Vec3f brdfSpecular = event.weight*event.pdf;
            float pdfSubstrate = cosineHemispherePdf(event.wo)*(1.0f - specularProbability);
            float pdfSpecular = event.pdf*specularProbability;

            event.weight = (brdfSpecular + brdfSubstrate)/(pdfSpecular + pdfSubstrate);
            event.pdf = pdfSpecular + pdfSubstrate;
        }
        return true;
    } else {
        Vec3f wo(cosineHemisphere(event.sampler->next2D()));
        float Fo = Fresnel::dielectricReflectance(eta, wo.z());
        Vec3f diffuseAlbedo = albedo(event.info);

        event.wo = wo;
        event.weight = ((1.0f - Fi)*(1.0f - Fo)*eta*eta)*(diffuseAlbedo/(1.0f - diffuseAlbedo*_diffuseFresnel));
        if (_scaledSigmaA.max() > 0.0f)
            event.weight *= std::exp(_scaledSigmaA*(-1.0f/event.wo.z() - 1.0f/event.wi.z()));

        event.pdf = cosineHemispherePdf(event.wo);
        if (sampleR) {
            Vec3f brdfSubstrate = event.weight*event.pdf;
            float  pdfSubstrate = event.pdf*(1.0f - specularProbability);
            Vec3f brdfSpecular = RoughDielectricBsdf::evalBase(event, true, false, (*_roughness)[*event.info].x(), _ior, _distribution);
            float pdfSpecular  = RoughDielectricBsdf::pdfBase(event, true, false, (*_roughness)[*event.info].x(), _ior, _distribution);
            pdfSpecular *= specularProbability;

            event.weight = (brdfSpecular + brdfSubstrate)/(pdfSpecular + pdfSubstrate);
            event.pdf = pdfSpecular + pdfSubstrate;
        }
        event.sampledLobe = BsdfLobes::DiffuseReflectionLobe;
    }
    return true;
}

Vec3f RoughPlasticBsdf::eval(const SurfaceScatterEvent &event) const
{
    bool sampleR = event.requestedLobe.test(BsdfLobes::GlossyReflectionLobe);
    bool sampleT = event.requestedLobe.test(BsdfLobes::DiffuseReflectionLobe);
    if (!sampleR && !sampleT)
        return Vec3f(0.0f);
    if (event.wi.z() <= 0.0f || event.wo.z() <= 0.0f)
        return Vec3f(0.0f);

    Vec3f glossyR(0.0f);
    if (sampleR)
        glossyR = RoughDielectricBsdf::evalBase(event, true, false, (*_roughness)[*event.info].x(), _ior, _distribution);

    Vec3f diffuseR(0.0f);
    if (sampleT) {
        float eta = 1.0f/_ior;
        float Fi = Fresnel::dielectricReflectance(eta, event.wi.z());
        float Fo = Fresnel::dielectricReflectance(eta, event.wo.z());

        Vec3f diffuseAlbedo = albedo(event.info);

        diffuseR = ((1.0f - Fi)*(1.0f - Fo)*eta*eta*event.wo.z()*INV_PI)*(diffuseAlbedo/(1.0f - diffuseAlbedo*_diffuseFresnel));
        if (_scaledSigmaA.max() > 0.0f)
            diffuseR *= std::exp(_scaledSigmaA*(-1.0f/event.wo.z() - 1.0f/event.wi.z()));
    }

    return glossyR + diffuseR;
}

float RoughPlasticBsdf::pdf(const SurfaceScatterEvent &event) const
{
    bool sampleR = event.requestedLobe.test(BsdfLobes::GlossyReflectionLobe);
    bool sampleT = event.requestedLobe.test(BsdfLobes::DiffuseReflectionLobe);
    if (!sampleR && !sampleT)
        return 0.0f;
    if (event.wi.z() <= 0.0f || event.wo.z() <= 0.0f)
        return 0.0f;

    float glossyPdf = 0.0f;
    if (sampleR)
        glossyPdf = RoughDielectricBsdf::pdfBase(event, true, false, (*_roughness)[*event.info].x(), _ior, _distribution);

    float diffusePdf = 0.0f;
    if (sampleT)
        diffusePdf = cosineHemispherePdf(event.wo);

    if (sampleT && sampleR) {
        float Fi = Fresnel::dielectricReflectance(1.0f/_ior, event.wi.z());
        float substrateWeight = _avgTransmittance*(1.0f - Fi);
        float specularWeight = Fi;
        float specularProbability = specularWeight/(specularWeight + substrateWeight);

        diffusePdf *= (1.0f - specularProbability);
        glossyPdf *= specularProbability;
    }
    return glossyPdf + diffusePdf;
}

void RoughPlasticBsdf::prepareForRender()
{
    _scaledSigmaA = _thickness*_sigmaA;
    _avgTransmittance = std::exp(-2.0f*_scaledSigmaA.avg());

    _distribution = Microfacet::stringToType(_distributionName);

    _diffuseFresnel = Fresnel::computeDiffuseFresnel(_ior, 1000000);
}
