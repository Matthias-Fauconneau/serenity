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
