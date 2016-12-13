#include "JsonSerializable.h"

#include "io/JsonUtils.h"

namespace Tungsten {

JsonSerializable::JsonSerializable(const std::string &name)
: _name(name)
{
}

void JsonSerializable::fromJson(const rapidjson::Value &v, const Scene &/*scene*/)
{
    JsonUtils::fromJson(v, "name", _name);
}

rapidjson::Value JsonSerializable::toJson(Allocator &allocator) const
{
    rapidjson::Value v(rapidjson::kObjectType);
    if (!unnamed())
        v.AddMember("name", JsonUtils::toJson(_name, allocator), allocator);
    return std::move(v);
}

}
