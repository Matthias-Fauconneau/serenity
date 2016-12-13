#include "Bsdf.h"

#include "materials/ConstantTexture.h"

#include "media/Medium.h"

#include "io/JsonObject.h"
#include "io/Scene.h"

namespace Tungsten {

Bsdf::Bsdf()
: _albedo(std::make_shared<ConstantTexture>(1.0f))
{
}

void Bsdf::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    JsonSerializable::fromJson(v, scene);

    scene.textureFromJsonMember(v, "albedo", TexelConversion::REQUEST_RGB, _albedo);
    scene.textureFromJsonMember(v, "bump", TexelConversion::REQUEST_AVERAGE, _bump);
}

rapidjson::Value Bsdf::toJson(Allocator &allocator) const
{
    JsonObject result{JsonSerializable::toJson(allocator), allocator,
        "albedo", *_albedo
    };
    if (_bump)
        result.add("bump", *_bump);

    return result;
}

}
