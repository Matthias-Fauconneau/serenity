#include "ThinSheetBsdf.h"
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

ThinSheetBsdf::ThinSheetBsdf()
: _ior(1.5f),
  _enableInterference(false),
  _thickness(std::make_shared<ConstantTexture>(0.5f)),
  _sigmaA(0.0f)
{
    _lobes = BsdfLobes(BsdfLobes::SpecularReflectionLobe | BsdfLobes::ForwardLobe);
}

void ThinSheetBsdf::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Bsdf::fromJson(v, scene);
    JsonUtils::fromJson(v, "ior", _ior);
    JsonUtils::fromJson(v, "enable_interference", _enableInterference);
    JsonUtils::fromJson(v, "sigma_a", _sigmaA);
    scene.textureFromJsonMember(v, "thickness", TexelConversion::REQUEST_AVERAGE, _thickness);
}

rapidjson::Value ThinSheetBsdf::toJson(Allocator &allocator) const
{
    return JsonObject{Bsdf::toJson(allocator), allocator,
        "type", "thinsheet",
        "ior", _ior,
        "enable_interference", _enableInterference,
        "thickness", *_thickness,
        "sigma_a", _sigmaA
    };
}

bool ThinSheetBsdf::sample(SurfaceScatterEvent &event) const
{
    if (!event.requestedLobe.test(BsdfLobes::SpecularReflectionLobe))
        return false;

    event.wo = Vec3f(-event.wi.x(), -event.wi.y(), event.wi.z());
    event.pdf = 1.0f;
    event.sampledLobe = BsdfLobes::SpecularReflectionLobe;

    if (_sigmaA == 0.0f && !_enableInterference) {
        // Fast path / early out
        event.weight = Vec3f(1.0f);
        return true;
    }

    float thickness = (*_thickness)[*event.info].x();

    float cosThetaT;
    if (_enableInterference) {
        event.weight = Fresnel::thinFilmReflectanceInterference(1.0f/_ior,
                std::abs(event.wi.z()), thickness*500.0f, cosThetaT);
    } else {
        event.weight = Vec3f(Fresnel::thinFilmReflectance(1.0f/_ior,
                std::abs(event.wi.z()), cosThetaT));
    }

    Vec3f transmittance = 1.0f - event.weight;
    if (_sigmaA != 0.0f && cosThetaT > 0.0f)
        transmittance *= std::exp(-_sigmaA*(thickness*2.0f/cosThetaT));

    event.weight /= 1.0f - transmittance.avg();

    return true;
}

Vec3f ThinSheetBsdf::eval(const SurfaceScatterEvent &event) const
{
    if (!event.requestedLobe.isForward() || -event.wi != event.wo)
        return Vec3f(0.0f);

    float thickness = (*_thickness)[*event.info].x();

    float cosThetaT;
    Vec3f transmittance;
    if (_enableInterference) {
        transmittance = 1.0f - Fresnel::thinFilmReflectanceInterference(1.0f/_ior,
                std::abs(event.wi.z()), thickness*500.0f, cosThetaT);
    } else {
        transmittance = Vec3f(1.0f - Fresnel::thinFilmReflectance(1.0f/_ior, std::abs(event.wi.z()), cosThetaT));
    }

    if (_sigmaA != 0.0f && cosThetaT > 0.0f)
        transmittance *= std::exp(-_sigmaA*(thickness*2.0f/cosThetaT));

    return transmittance;
}

float ThinSheetBsdf::pdf(const SurfaceScatterEvent &event) const
{
    bool sampleR = event.requestedLobe.test(BsdfLobes::SpecularReflectionLobe);
    if (sampleR && checkReflectionConstraint(event.wi, event.wo))
        return 1.0f;
    else
        return 0.0f;
}
