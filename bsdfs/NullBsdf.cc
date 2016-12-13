#include "NullBsdf.h"
#include "io/JsonObject.h"


NullBsdf::NullBsdf()
{
    _lobes = BsdfLobes::NullLobe;
}

rapidjson::Value NullBsdf::toJson(Allocator &allocator) const
{
    return JsonObject{Bsdf::toJson(allocator), allocator,
        "type", "null"
    };
}

bool NullBsdf::sample(SurfaceScatterEvent &/*event*/) const
{
    return false;
}

Vec3f NullBsdf::eval(const SurfaceScatterEvent &/*event*/) const
{
    return Vec3f(0.0f);
}

float NullBsdf::pdf(const SurfaceScatterEvent &/*event*/) const
{
    return 0.0f;
}
