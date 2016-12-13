#include "Texture.h"
#include "io/JsonUtils.h"

bool Texture::scalarOrVecFromJson(const rapidjson::Value &v, const char *field, Vec3f &dst)
{
    float scalar;
    if (::fromJson(v, field, scalar)) {
        dst = Vec3f(scalar);
        return true;
    } else {
        return ::fromJson(v, field, dst);
    }
}

rapidjson::Value Texture::scalarOrVecToJson(const Vec3f &src, Allocator &allocator)
{
    if (src.x() == src.y() && src.y() == src.z())
        return std::move(toJson(src.x(), allocator));
    else
        return std::move(toJson(src, allocator));
}
