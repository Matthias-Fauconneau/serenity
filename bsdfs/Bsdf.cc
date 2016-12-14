#include "Bsdf.h"
#include "materials/ConstantTexture.h"
#include "media/Medium.h"
#include "io/JsonObject.h"
#include "io/Scene.h"

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
