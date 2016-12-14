#include "IsotropicPhaseFunction.h"
#include "sampling/PathSampleGenerator.h"
#include "sampling/SampleWarp.h"
#include "io/JsonObject.h"

Vec3f IsotropicPhaseFunction::eval(const Vec3f &/*wi*/, const Vec3f &/*wo*/) const
{
    return Vec3f(INV_FOUR_PI);
}

bool IsotropicPhaseFunction::sample(PathSampleGenerator &sampler, const Vec3f &/*wi*/, PhaseSample &sample) const
{
    sample.w = uniformSphere(sampler.next2D());
    sample.weight = Vec3f(1.0f);
    sample.pdf = uniformSpherePdf();
    return true;
}

float IsotropicPhaseFunction::pdf(const Vec3f &/*wi*/, const Vec3f &/*wo*/) const
{
    return uniformSpherePdf();
}
