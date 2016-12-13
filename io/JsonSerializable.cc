#include "JsonSerializable.h"

#include "io/JsonUtils.h"

JsonSerializable::JsonSerializable(const std::string &name)
: _name(name)
{
}

void JsonSerializable::fromJson(const rapidjson::Value &v, const Scene &/*scene*/)
{
    ::fromJson(v, "name", _name);
}

rapidjson::Value JsonSerializable::toJson(Allocator &allocator) const
{
    rapidjson::Value v(rapidjson::kObjectType);
    if (!unnamed())
        v.AddMember("name", ::toJson(_name, allocator), allocator);
    return v;
}
