#include "ConductorBsdf.h"
#include "ComplexIor.h"
#include "Fresnel.h"
#include "samplerecords/SurfaceScatterEvent.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/SampleWarp.h"
#include "math/MathUtil.h"
#include "math/Angle.h"
#include "math/Vec.h"
#include "io/JsonObject.h"
#include <rapidjson/document.h>

ConductorBsdf::ConductorBsdf()
: _materialName("Cu"),
  _eta(0.200438f, 0.924033f, 1.10221f),
  _k(3.91295f, 2.45285f, 2.14219f)
{
    _lobes = BsdfLobes(BsdfLobes::SpecularReflectionLobe);
}

void ConductorBsdf::lookupMaterial()
{
    if (!ComplexIorList::lookup(_materialName, _eta, _k)) {
        DBG("Warning: Unable to find material with name '%s'. Using default", _materialName.c_str());
        ComplexIorList::lookup("Cu", _eta, _k);
    }
}

void ConductorBsdf::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Bsdf::fromJson(v, scene);
    if (::fromJson(v, "eta", _eta) && ::fromJson(v, "k", _k))
        _materialName.clear();
    if (::fromJson(v, "material", _materialName))
        lookupMaterial();
}

rapidjson::Value ConductorBsdf::toJson(Allocator &allocator) const
{
    JsonObject result{Bsdf::toJson(allocator), allocator,
        "type", "conductor"
    };
    if (_materialName.empty())
        result.add("eta", _eta, "k", _k);
    else
        result.add("material", _materialName);

    return result;
}

bool ConductorBsdf::sample(SurfaceScatterEvent &event) const
{
    if (!event.requestedLobe.test(BsdfLobes::SpecularReflectionLobe))
        return false;

    event.wo = Vec3f(-event.wi.x(), -event.wi.y(), event.wi.z());
    event.pdf = 1.0f;
    event.weight = albedo(event.info)*Fresnel::conductorReflectance(_eta, _k, event.wi.z());
    event.sampledLobe = BsdfLobes::SpecularReflectionLobe;
    return true;
}

Vec3f ConductorBsdf::eval(const SurfaceScatterEvent &event) const
{
    bool evalR = event.requestedLobe.test(BsdfLobes::SpecularReflectionLobe);
    if (evalR && checkReflectionConstraint(event.wi, event.wo))
        return albedo(event.info)*Fresnel::conductorReflectance(_eta, _k, event.wi.z());
    else
        return Vec3f(0.0f);
}

float ConductorBsdf::pdf(const SurfaceScatterEvent &event) const
{
    bool sampleR = event.requestedLobe.test(BsdfLobes::SpecularReflectionLobe);
    if (sampleR && checkReflectionConstraint(event.wi, event.wo))
        return 1.0f;
    else
        return 0.0f;
}
