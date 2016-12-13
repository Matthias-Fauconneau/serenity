#include "RayleighPhaseFunction.h"
#include "sampling/PathSampleGenerator.h"
#include "math/TangentFrame.h"
#include "math/MathUtil.h"
#include "math/Angle.h"
#include "io/JsonObject.h"

inline float RayleighPhaseFunction::rayleigh(float cosTheta)
{
    return (3.0f/(16.0f*PI))*(1.0f + cosTheta*cosTheta);
}

rapidjson::Value RayleighPhaseFunction::toJson(Allocator &allocator) const
{
    return JsonObject{PhaseFunction::toJson(allocator), allocator,
        "type", "rayleigh"
    };
}

Vec3f RayleighPhaseFunction::eval(const Vec3f &wi, const Vec3f &wo) const
{
    return Vec3f(rayleigh(wi.dot(wo)));
}

bool RayleighPhaseFunction::sample(PathSampleGenerator &sampler, const Vec3f &wi, PhaseSample &sample) const
{
    Vec2f xi = sampler.next2D();
    float phi = xi.x()*TWO_PI;
    float z = xi.y()*4.0f - 2.0f;
    float invZ = std::sqrt(z*z + 1.0f);
    float u = std::cbrt(z + invZ);
    float cosTheta = u - 1.0f/u;

    float sinTheta = std::sqrt(max(1.0f - cosTheta*cosTheta, 0.0f));
    sample.w = TangentFrame(wi).toGlobal(Vec3f(
        std::cos(phi)*sinTheta,
        std::sin(phi)*sinTheta,
        cosTheta
    ));
    sample.weight = Vec3f(1.0f);
    sample.pdf = rayleigh(cosTheta);
    return true;
}

float RayleighPhaseFunction::pdf(const Vec3f &wi, const Vec3f &wo) const
{
    return rayleigh(wi.dot(wo));
}
