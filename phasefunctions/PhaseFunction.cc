#include "PhaseFunction.h"

void PhaseFunction::fromJson(const rapidjson::Value &/*v*/, const Scene &/*scene*/)
{
}

rapidjson::Value PhaseFunction::toJson(Allocator &allocator) const
{
    return JsonSerializable::toJson(allocator);
}
